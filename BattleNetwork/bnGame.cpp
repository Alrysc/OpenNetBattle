#include <time.h>
#include <queue>
#include <atomic>
#include <cmath>
#include <Swoosh/ActivityController.h>
#include <Swoosh/Ease.h>

#include "bnGame.h"
#include "bnGameSession.h"
#include "bnPlayerPackageManager.h"
#include "bnCardPackageManager.h"
#include "bnMobPackageManager.h"
#include "bnBlockPackageManager.h"
#include "bnLuaLibraryPackageManager.h"
#include "bnGameOverScene.h"
#include "bnFakeScene.h"
#include "bnConfigReader.h"
#include "bnFont.h"
#include "bnText.h"
#include "bnQueueModRegistration.h"
#include "bnResourceHandle.h"
#include "bnInputHandle.h"
#include "overworld/bnOverworldHomepage.h"
#include "SFML/System.hpp"

#include "bindings/bnScriptedBlock.h"
#include "bindings/bnScriptedCard.h"
#include "bindings/bnScriptedPlayer.h"
#include "bindings/bnScriptedMob.h"
#include "bindings/bnLuaLibrary.h"

#include "sago/platform_folders.h"

Game::Game(DrawWindow& window) :
  window(window), 
  reader("config.ini"),
  configSettings(),
  netManager(),
  textureManager(),
  audioManager(),
  shaderManager(),
  inputManager(*window.GetRenderWindow()),
  ActivityController(*window.GetRenderWindow()) {
  
  // figure out system endianness
  {
    int n = 1;
    // little endian if true
    if (*(char*)&n == 1) { endian = Endianness::little; }
  }

  // Link the resource handle to use all the manangers created by the game
  ResourceHandle::audio    = &audioManager;
  ResourceHandle::textures = &textureManager;
  ResourceHandle::shaders  = &shaderManager;

#ifdef BN_MOD_SUPPORT
  ResourceHandle::scripts  = &scriptManager;
#endif

  // Link i/o handle to use the input manager created by the game
  InputHandle::input = &inputManager;

  // Link net handle to use manager created by the game
  NetHandle::net = &netManager;

  // Create package managers for rest of game to utilize
  cardPackageManager = new class CardPackageManager;
  playerPackageManager = new class PlayerPackageManager;
  mobPackageManager = new class MobPackageManager;
  blockPackageManager = new class BlockPackageManager;
  luaLibraryPackageManager = new class LuaLibraryPackageManager;

  // Script bindings needs to know about other sub system
  scriptManager.SetCardPackageManager(*cardPackageManager);

  // Game session object needs to be available to every scene
  session = new GameSession;
  session->SetCardPackageManager(*cardPackageManager);

  // Use the engine's window settings for this platform to create a properly 
  // sized render surface...
  unsigned int win_x = static_cast<unsigned int>(window.GetView().getSize().x);
  unsigned int win_y = static_cast<unsigned int>(window.GetView().getSize().y);

  renderSurface.create(win_x, win_y, window.GetRenderWindow()->getSettings());

  // Use our external render surface as the game's screen
  window.SetRenderSurface(renderSurface);
  window.GetRenderWindow()->setActive(false);
}

Game::~Game() {
  if (renderThread.joinable()) {
    renderThread.join();
  }

  if (recordOutThread.joinable()) {
    recordOutThread.join();
  }

  delete session;
  delete cardPackageManager;
  delete playerPackageManager;
  delete mobPackageManager;
  delete blockPackageManager;
  delete luaLibraryPackageManager;
}

void Game::SetCommandLineValues(const cxxopts::ParseResult& values) {
  commandline = &values;

  // Now that we have CLI values, we can configure 
  // other subsystems that need to read from them...
  unsigned int myPort = CommandLineValue<int>("port");
  uint16_t maxPayloadSize = CommandLineValue<uint16_t>("mtu");
  netManager.BindPort(myPort);

  if (maxPayloadSize != 0) {
    netManager.SetMaxPayloadSize(maxPayloadSize);
  }
}

TaskGroup Game::Boot(const cxxopts::ParseResult& values)
{
  SeedRand((unsigned int)time(0));
  isDebug = CommandLineValue<bool>("debug");
  singlethreaded = CommandLineValue<bool>("singlethreaded");

  if (reader.IsOK()) {
    Logger::Log(LogLevel::warning, "config settings was not OK. Will use internal default key layout.");
  }

  // Initialize the engine and log the startup time
  const clock_t begin_time = clock();

  /**
  * TODO
  DrawWindow::WindowMode mode = configSettings.IsFullscreen() ? DrawWindow::WindowMode::fullscreen : DrawWindow::WindowMode::window;

  window.Initialize(windowMode);
  */

  Logger::Logf(LogLevel::info, "Engine initialized: %f secs", float(clock() - begin_time) / CLOCKS_PER_SEC);

  // does shaders too
  Callback<void()> graphics;
  graphics.Slot(std::bind(&Game::RunGraphicsInit, this, &progress));

  Callback<void()> audio;
  audio.Slot(std::bind(&Game::RunAudioInit, this, &progress));

  Callback<void()> libraries;
  libraries.Slot( std::bind( &Game::RunLuaLibraryInit, this, &progress ) );

  Callback<void()> navis;
  navis.Slot(std::bind(&Game::RunNaviInit, this, &progress));

  Callback<void()> mobs;
  mobs.Slot(std::bind(&Game::RunMobInit, this, &progress));

  Callback<void()> cards;
  cards.Slot(std::bind(&Game::RunCardInit, this, &progress));

  Callback<void()> blocks;
  blocks.Slot(std::bind(&Game::RunBlocksInit, this, &progress));

  Callback<void()> resolveFailedPackages;
  resolveFailedPackages.Slot([this]() {
    std::string package = cardPackageManager->FirstValidPackage();

    do {
      if (package.empty()) break;

      scriptManager.DefineCard(package, cardPackageManager->FindPackageByID(package).GetFilePath());

      package = cardPackageManager->GetPackageAfter(package);
    } while (package != cardPackageManager->FirstValidPackage());

    auto packages = ResolveFailedPackages();

    while (!packages.empty()) { 
      for (auto& p : packages) {
        scriptManager.DefineCard(cardPackageManager->FilepathToPackageID(p), p);
      }

      /* keep trying */ 
      packages = ResolveFailedPackages();
    }
  });

  Callback<void()> init;
  init.Slot([this] {
    // Tell the input event loop how to behave when the app loses and regains focus
    inputManager.BindLoseFocusEvent(std::bind(&Game::LoseFocus, this));
    inputManager.BindRegainFocusEvent(std::bind(&Game::GainFocus, this));
    inputManager.BindResizedEvent(std::bind(&Game::Resize, this, std::placeholders::_1, std::placeholders::_2));

    Font::specialCharLookup.insert(std::make_pair(char(-1), "THICK_SP"));
    Font::specialCharLookup.insert(std::make_pair(char(-2), "THICK_EX"));
    Font::specialCharLookup.insert(std::make_pair(char(-3), "THICK_NM"));

    // See the random generator with current time
    this->SeedRand((unsigned int)time(0));
  });

  this->UpdateConfigSettings(reader.GetConfigSettings());

  TaskGroup tasks;
  tasks.AddTask("Binding window", std::move(init));
  tasks.AddTask("Init graphics", std::move(graphics));
  tasks.AddTask("Init audio", std::move(audio));
  tasks.AddTask("Load Libraries", std::move( libraries ) );
  tasks.AddTask("Load Navis", std::move(navis));
  tasks.AddTask("Load mobs", std::move(mobs));
  tasks.AddTask("Load cards", std::move(cards));
  tasks.AddTask("Load prog blocks", std::move(blocks));
  tasks.AddTask("Resolving packages", std::move(resolveFailedPackages));

  // Load font symbols immediately...
  textureManager.LoadFromFile(TexturePaths::FONT);

  mouseTexture = textureManager.LoadFromFile("resources/ui/mouse.png");
  mouse.setTexture(mouseTexture);
  mouseAnimation = Animation("resources/ui/mouse.animation");
  mouseAnimation << "DEFAULT" << Animator::Mode::Loop;
  mouseAlpha = 1.0;

  window.GetRenderWindow()->setMouseCursorVisible(false);

  // set a loading spinner on the bottom-right corner of the screen
  spinner.setTexture(textureManager.LoadFromFile("resources/ui/spinner.png"));
  spinner.setScale(2.f, 2.f);
  spinner.setPosition(float(window.GetView().getSize().x - 64), float(window.GetView().getSize().y - 50));

  spinnerAnimator = Animation("resources/ui/spinner.animation") << "SPIN" << Animator::Mode::Loop;

  if (!singlethreaded) {
    renderThread = std::thread(&Game::ProcessFrame, this);
  }

  return tasks;
}

bool Game::NextFrame()
{
  bool nextFrameKey = inputManager.Has(InputEvents::pressed_advance_frame);
  bool resumeKey = inputManager.Has(InputEvents::pressed_resume_frames);

  if (nextFrameKey && isDebug && !frameByFrame) {
    frameByFrame = true;
  }
  else if (resumeKey && frameByFrame) {
    frameByFrame = false;
  }

  return (frameByFrame && nextFrameKey) || !frameByFrame;
}

void Game::HandleRecordingEvents()
{
  if (isRecordOutSaving)
    return;

  if (!inputManager.Has(InputEvents::pressed_record_frames)) {
    recordPressed = false;
    return;
  }

  if (recordPressed)
    return;

  recordPressed = true;

  if (!IsRecording()) {
    Record(true);
    window.SetSubtitle("[RECORDING]");
    return;
  }

  recordOutThread = std::thread([this]() {
    isRecordOutSaving = true;
    Record(false);

    Logger::Logf(LogLevel::info, "Saving recording to disk, please wait.");
    window.SetSubtitle("[SAVING]");

    for (auto& pair : recordedFrames) {
      pair.second.saveToFile("recording/frame_" + std::to_string(pair.first) + ".png");
    }

    Logger::Logf(LogLevel::info, "Wrote %i frames to disk", (int)recordedFrames.size());
    recordedFrames.clear();
    window.SetSubtitle("");

    isRecordOutSaving = false;
  });

  recordOutThread.detach();
}

void Game::UpdateMouse(double dt)
{
  auto& renderWindow = *window.GetRenderWindow();
  sf::Vector2f mousepos = renderWindow.mapPixelToCoords(sf::Mouse::getPosition(renderWindow));

  mouse.setPosition(mousepos);
  mouse.setColor(sf::Color::White);
  mouseAnimation.Update(dt, mouse.getSprite());
}

void Game::ProcessFrame()
{
  sf::Clock clock;
  float scope_elapsed = 0.0f;
  window.GetRenderWindow()->setActive(true);

  while (!quitting) {
    clock.restart();

    double delta = 1.0 / static_cast<double>(frame_time_t::frames_per_second);
    this->elapsed += from_seconds(delta);

    // Poll net code
    netManager.Update(delta);

    if (NextFrame()) {
      window.Clear(); // clear screen

      inputManager.Update(); // process inputs
      HandleRecordingEvents();
      UpdateMouse(delta);
      this->update(delta);  // update game logic
      this->draw();        // draw game
      mouse.draw(*window.GetRenderWindow());
      window.Display(); // display to screen

      if (isRecording) {
        sf::Image image = window.GetRenderWindow()->capture();
        recordedFrames.push_back(std::pair(FrameNumber(), image));
      }
    }

    scope_elapsed = clock.getElapsedTime().asSeconds();
  }
}

void Game::RunSingleThreaded()
{
  sf::Clock clock;
  float scope_elapsed = 0.0f;
  window.GetRenderWindow()->setActive(true);

  while (window.Running() && !quitting) {
    clock.restart();
    this->SeedRand(time(0));

    // Poll window events
    inputManager.EventPoll();

    // unused images need to be free'd 
    textureManager.HandleExpiredTextureCache();

    double delta = 1.0 / static_cast<double>(frame_time_t::frames_per_second);
    this->elapsed += from_seconds(delta);

    // Poll net code
    netManager.Update(delta);

    if (NextFrame()) {
      window.Clear(); // clear screen

      inputManager.Update(); // process inputs

      HandleRecordingEvents();

      UpdateMouse(delta);
      this->update(delta);  // update game logic
      this->draw();        // draw game
      mouse.draw(*window.GetRenderWindow());
      window.Display(); // display to screen
    }

    quitting = getStackSize() == 0;

    scope_elapsed = clock.getElapsedTime().asSeconds();
  }
}

void Game::Exit()
{
  quitting = true;
}

void Game::Run()
{
  if (singlethreaded) {
    RunSingleThreaded();
    return;
  }  

  while (window.Running() && !quitting) {
    this->SeedRand(time(0));

    // Poll window events
    inputManager.EventPoll();

    // unused images need to be free'd 
    textureManager.HandleExpiredTextureCache();

    quitting = getStackSize() == 0;
  }
}

void Game::SetWindowMode(DrawWindow::WindowMode mode)
{
  windowMode = mode;
}

void Game::Postprocess(ShaderType shaderType)
{
  this->postprocess = shaderManager.GetShader(shaderType);
}

void Game::NoPostprocess()
{
  this->postprocess = nullptr;
}

void Game::PrintCommandLineArgs()
{
  Logger::Log(LogLevel::debug, "Command line args provided");
  for (auto&& kv : commandlineArgs) {
    Logger::Logf(LogLevel::debug, "%s : %s", kv.key().c_str(), kv.value().c_str());
  }
}

const sf::Vector2f Game::CameraViewOffset(Camera& camera)
{
  return window.GetView().getCenter() - camera.GetView().getCenter();
}

unsigned Game::FrameNumber() const
{
  return this->elapsed.count();
}

const Endianness Game::GetEndianness()
{
  return endian;
}

void Game::LoadConfigSettings()
{
  // try to read the config file
  configSettings = reader.GetConfigSettings();
}

void Game::UpdateConfigSettings(const struct ConfigSettings& new_settings)
{
  if (!new_settings.IsOK())
    return;

  configSettings = new_settings;

  if (configSettings.GetShaderLevel() > 0) {
    window.SupportShaders(true);
    ActivityController::optimizeForPerformance(swoosh::quality::realtime);
    ActivityController::enableShaders(true);
  }
  else {
    window.SupportShaders(false);
    ActivityController::optimizeForPerformance(swoosh::quality::mobile);
    ActivityController::enableShaders(false);
  }

  // If the file is good, use the Audio() and 
  // controller settings from the config
  audioManager.EnableAudio(configSettings.IsAudioEnabled());
  audioManager.SetStreamVolume(((configSettings.GetMusicLevel()-1) / 3.0f) * 100.0f);
  audioManager.SetChannelVolume(((configSettings.GetSFXLevel()-1) / 3.0f) * 100.0f);

  inputManager.SupportConfigSettings(configSettings);
}

void Game::SeedRand(unsigned int seed)
{
  randSeed = seed;
  scriptManager.SeedRand(seed);
}

const unsigned int Game::GetRandSeed() const
{
  return randSeed;
}

bool Game::IsSingleThreaded() const
{
  return singlethreaded;
}

bool Game::IsRecording() const
{
  return isRecording;
}

void Game::Record(bool enabled)
{
  isRecording = enabled;
}

const std::string Game::AppDataPath()
{
  return sago::getDataHome() + "/" + window.GetTitle();
}

const std::string Game::CacheDataPath()
{
  return sago::getCacheDir() + "/" + window.GetTitle();
}

const std::string Game::DesktopPath()
{
  return sago::getDesktopFolder();
}

const std::string Game::DownloadsPath()
{
  return sago::getDownloadFolder();
}

const std::string Game::DocumentsPath()
{
  return sago::getDocumentsFolder();
}

const std::string Game::VideosPath()
{
  return sago::getVideoFolder();
}

const std::string Game::PicturesPath()
{
  return sago::getPicturesFolder();
}

const std::string Game::SaveGamesPath()
{
  return sago::getSaveGamesFolder1();
}

CardPackageManager& Game::CardPackageManager()
{
  return *cardPackageManager;
}

PlayerPackageManager& Game::PlayerPackageManager()
{
  return *playerPackageManager;
}

MobPackageManager& Game::MobPackageManager()
{
  return *mobPackageManager;
}

BlockPackageManager& Game::BlockPackageManager()
{
  return *blockPackageManager;
}

LuaLibraryPackageManager& Game::GetLuaLibraryPackageManager()
{
  return *luaLibraryPackageManager;
}

ConfigSettings& Game::ConfigSettings()
{
  return configSettings;
}

GameSession& Game::Session()
{
  return *session;
}

void Game::RunNaviInit(std::atomic<int>* progress) {
  clock_t begin_time = clock();

  auto LoadPlayerMods = QueueModRegistration<class PlayerPackageManager, ScriptedPlayer>;
  LoadPlayerMods(*playerPackageManager, "resources/mods/players", "Player Mods");
  playerPackageManager->LoadAllPackages(*progress);

  Logger::Logf(LogLevel::info, "Loaded registered navis: %f secs", float(clock() - begin_time) / CLOCKS_PER_SEC);
}

void Game::RunBlocksInit(std::atomic<int>* progress)
{
  clock_t begin_time = clock();

  auto LoadBlockMods = QueueModRegistration<class BlockPackageManager, ScriptedBlock>;
  LoadBlockMods(*blockPackageManager, "resources/mods/blocks", "Prog Block Mods");
  blockPackageManager->LoadAllPackages(*progress);

  Logger::Logf(LogLevel::info, "Loaded registered prog blocks: %f secs", float(clock() - begin_time) / CLOCKS_PER_SEC);
}

void Game::RunMobInit(std::atomic<int>* progress) {
  clock_t begin_time = clock();

  auto LoadEnemyMods = QueueModRegistration<class MobPackageManager, ScriptedMob>;
  LoadEnemyMods(*mobPackageManager, "resources/mods/enemies", "Enemy Mods");
  mobPackageManager->LoadAllPackages(*progress);

  Logger::Logf(LogLevel::info, "Loaded registered mobs: %f secs", float(clock() - begin_time) / CLOCKS_PER_SEC);
}

void Game::RunCardInit(std::atomic<int>* progress) {
  clock_t begin_time = clock();

  auto LoadCardMods = QueueModRegistration<class CardPackageManager, ScriptedCard>;
  LoadCardMods(*cardPackageManager, "resources/mods/cards", "Card Mods");
  cardPackageManager->LoadAllPackages(*progress);

  Logger::Logf(LogLevel::info, "Loaded registered cards: %f secs", float(clock() - begin_time) / CLOCKS_PER_SEC);
}

void Game::RunLuaLibraryInit(std::atomic<int>* progress) {
  clock_t begin_time = clock();

  auto LoadCoreLibraryMods = QueueModRegistration<class LuaLibraryPackageManager, LuaLibrary>;
  LoadCoreLibraryMods(*luaLibraryPackageManager, "resources/mods/libs", "Core Libs Mods");
  luaLibraryPackageManager->LoadAllPackages(*progress);

  Logger::Logf(LogLevel::info, "Loaded registered libraries: %f secs", float(clock() - begin_time) / CLOCKS_PER_SEC);
}

void Game::RunGraphicsInit(std::atomic<int> * progress) {
  clock_t begin_time = clock();
  textureManager.LoadAllTextures(*progress);

  Logger::Logf(LogLevel::info, "Loaded textures: %f secs", float(clock() - begin_time) / CLOCKS_PER_SEC);

  if (reader.GetConfigSettings().GetShaderLevel() > 0) {
    ActivityController::optimizeForPerformance(swoosh::quality::realtime);
    begin_time = clock();
    shaderManager.LoadAllShaders(*progress);

    Logger::Logf(LogLevel::info, "Loaded shaders: %f secs", float(clock() - begin_time) / CLOCKS_PER_SEC);
  }
  else {
    ActivityController::optimizeForPerformance(swoosh::quality::mobile);
    Logger::Log(LogLevel::info, "Shader support is disabled");
  }
}

void Game::RunAudioInit(std::atomic<int> * progress) {
  const clock_t begin_time = clock();
  audioManager.LoadAllSources(*progress);

  Logger::Logf(LogLevel::info, "Loaded Audio() sources: %f secs", float(clock() - begin_time) / CLOCKS_PER_SEC);
}

void Game::GainFocus() {
  window.RegainFocus();
  audioManager.Mute(false);

#ifdef __ANDROID__
  // TODO: Reload all graphics and somehow reassign all gl IDs to all allocated sfml graphics structures
  // Textures().RecoverLostGLContext();
  // ENGINE.RecoverLostGLContext(); // <- does the window need recreation too?
#endif
}

void Game::LoseFocus() {
  audioManager.Mute(true);
}

void Game::Resize(int newWidth, int newHeight) {
  float windowRatio = (float)newWidth / (float)newHeight;
  float viewRatio = window.GetView().getSize().x / window.GetView().getSize().y;

  showScreenBars = windowRatio >= viewRatio;
  window.Resize(newWidth, newHeight);
}