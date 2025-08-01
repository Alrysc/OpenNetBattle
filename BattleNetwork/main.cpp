#include "bnGame.h"
#include "battlescene/bnMobBattleScene.h"
#include "bindings/bnScriptedMob.h"
#include "bindings/bnScriptedBlock.h"
#include "bindings/bnScriptedPlayer.h"
#include "bindings/bnScriptedCard.h"
#include "bindings/bnLuaLibrary.h"
#include "bnPlayerPackageManager.h"
#include "bnMobPackageManager.h"
#include "bnBlockPackageManager.h"
#include "bnCardPackageManager.h"
#include "bnLuaLibraryPackageManager.h"
#include "bnGameOverScene.h"
#include "bnTitleScene.h"
#include "bnACDCBackground.h"
#include "bnMob.h"
#include "bnPlayer.h"
#include "bnEmotions.h"
#include "bnCardFolder.h"
#include "stx/string.h"
#include "stx/result.h"
#include "cxxopts/cxxopts.hpp"
#include "netplay/bnNetPlayConfig.h"

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/URI.h>
#include <Poco/StreamCopier.h>

// Launches the standard game with full setup and configuration
int LaunchGame(Game& g, const cxxopts::ParseResult& results);

// Prepares launching the game in an isolated battle-only mode
int HandleBattleOnly(Game& g, TaskGroup tasks, const std::string& playerpath, const std::string& mobpath, const std::string& folderPath, bool isURL);

// (experimental) will download a mod from a URL
template<typename ScriptedDataType, typename PackageManager>
stx::result_t<std::string> DownloadPackageFromURL(const std::string& url, PackageManager& packageManager);

// Feeds battle-mode with a list of card mods for ez testing
std::unique_ptr<CardFolder> LoadFolderFromFile(const std::string& filePath, CardPackageManager& packageManager);

// If filter is empty, lists all packages and hash pairs.
void PrintPackageHash(Game& g, TaskGroup tasks);

// Reads a zip mod on disk and displays the package ID and hash
void ReadPackageAndHash(const std::string& path, const std::string& modType);

static cxxopts::Options options("ONB", "Open Net Battle Engine");

int main(int argc, char** argv) {
  // Create help and other generic flags
  options.add_options()
    ("h,help", "Print all options")
    ("e,errorLevel", "Set the level to filter error messages [silent|info|warning|critical|debug] (default is `critical`)", cxxopts::value<std::string>()->default_value("warning|critical"))
    ("d,debug", "Enable debugging")
    ("s,singlethreaded", "run logic and draw routines in a single, main thread")
    ("l,locale", "set flair and language to desired target", cxxopts::value<std::string>()->default_value("en"))
    ("p,port", "port for PVP", cxxopts::value<int>()->default_value("0"))
    ("r,remotePort", "remote port for main hub", cxxopts::value<int>()->default_value(std::to_string(NetPlayConfig::OBN_PORT)))
    ("w,cyberworld", "ip address of main hub", cxxopts::value<std::string>()->default_value(""))
    ("m,mtu", "Maximum Transmission Unit - adjust to send big packets", cxxopts::value<uint16_t>()->default_value(std::to_string(NetManager::DEFAULT_MAX_PAYLOAD_SIZE)));

  // Battle-only specific flags
  options.add_options("Battle Only Mode")
    ("b,battleonly", "Jump into a battle from a package")
    ("mob", "path to mob package", cxxopts::value<std::string>()->default_value(""))
    ("moburl", "path to mob file to download from a web address", cxxopts::value<std::string>()->default_value(""))
    ("player", "name of player package", cxxopts::value<std::string>()->default_value(""))
    ("folder", "path to folder list on disk where each line contains a card package name and code e.g. `com.example.MockCard A`", cxxopts::value<std::string>()->default_value(""));

  // Utility specific flags
  options.add_options("Utilities")
    ("i,installed", "List the successfully loaded mods and their hashes")
    ("j,hash", "path to a mod .zip anywhere on disk then display the md5 and package id pair to screen", cxxopts::value<std::string>()->default_value(""))
    ("t,type", "specifies the one type of mod to parse [player|block|card|mob|lib]", cxxopts::value<std::string>()->default_value(""));

  // Prevent throwing exceptions on bad input
  options.allow_unrecognised_options();

 try {
    cxxopts::ParseResult parsedOptions = options.parse(argc, argv);

    // Check for help, print, and quit early
    if (parsedOptions.count("help")) {
      std::cout << options.help() << std::endl;
      return EXIT_SUCCESS;
    }

    DrawWindow win;
    win.Initialize("Open Net Battle v" + std::string(Game::Version), DrawWindow::WindowMode::window);
    Game game{ win };

    // Go the the title screen to kick off the rest of the app
    if (LaunchGame(game, parsedOptions) == EXIT_SUCCESS) {
      // blocking
      game.Run();
    }
  }
  catch (cxxopts::missing_argument_exception& e) {
    Logger::Log(LogLevel::critical, e.what());
  }
  catch (std::exception& e) {
    Logger::Log(LogLevel::critical, e.what());
  }
  catch (...) {
    Logger::Log(LogLevel::critical, "Game encountered an unknown exception. Aborting.");
  }

  // finished
  return EXIT_SUCCESS;
}

void ParseErrorLevel(std::string in) {
  std::map<std::string, bool> settings;

  // Parse input tokens
  // Levels are separated by pipes | and lowercased
  in = stx::replace(in, " ", ""); // trimmed whitespace
  std::vector<std::string> tokens = stx::tokenize(in, '|');

  for (std::string& s : tokens) {
    settings[s] = true;
  }

  uint8_t level = LogLevel::silent;

  std::string msg = "Logs will be displayed below. Log level is set to ";
  std::vector<std::string> valid;

  auto processSettings = [&settings, &level, &valid](const std::string& key, uint8_t value) {
    if (settings[key]) {
      level |= value;
      valid.push_back(key);
    }
  };

  processSettings("critical", LogLevel::critical);
  processSettings("warning", LogLevel::warning);
  processSettings("debug", LogLevel::debug);
  processSettings("info", LogLevel::info);
  processSettings("net", LogLevel::net);


  if (settings["all"]) {
    level = LogLevel::all;
    valid.clear();
    valid.push_back("all");
  }
  
  std::string validStr = "silent";

  for (size_t i = 0; i < valid.size(); i++) {
    validStr += valid[i];

    if (i + 1u < valid.size()) {
      validStr += "|";
    }
  }

  msg += "`" + validStr + "`";
  std::cerr << msg << std::endl;
  
  Logger::SetLogLevel(level);
}

int LaunchGame(Game& g, const cxxopts::ParseResult& results) {
  g.SeedRand((unsigned int)time(0));
  g.SetCommandLineValues(results);

  ParseErrorLevel(g.CommandLineValue<std::string>("errorLevel"));

  g.PrintCommandLineArgs();

  if (g.GetEndianness() == Endianness::big) {
    Logger::Log(LogLevel::info, "System arch is Big Endian");
  }
  else {
    Logger::Log(LogLevel::info, "System arch is Little Endian");
  }

  if (g.CommandLineValue<bool>("battleonly")) {
    std::string playerpath = g.CommandLineValue<std::string>("player");
    std::string mobpath = g.CommandLineValue<std::string>("mob");
    std::string moburl = g.CommandLineValue<std::string>("moburl");
    std::string folderpath = g.CommandLineValue<std::string>("folder");
    bool url = false;

    if (playerpath.empty()) {
      Logger::Logf(LogLevel::critical, "Battleonly mode needs `player` input argument");
      return EXIT_FAILURE;
    }

    if (mobpath.empty() && moburl.empty()) {
      Logger::Logf(LogLevel::critical, "Battleonly mode needs `mob` or `moburl` input argument");
      return EXIT_FAILURE;
    }

    if (moburl.size()) {
      mobpath = moburl;
      url = true;
    }

    return HandleBattleOnly(g, g.Boot(results), playerpath, mobpath, folderpath, url);
  }

  if (g.CommandLineValue<bool>("installed")) {
    PrintPackageHash(g, g.Boot(results));

    return EXIT_SUCCESS;
  }

  const std::string& path = g.CommandLineValue<std::string>("hash");
  const std::string& type = g.CommandLineValue<std::string>("type");
  if (!path.empty()) {
    if (type.empty()) {
      std::cerr << "Please specify the type of mod. Use -h for options." << std::endl;
      return EXIT_FAILURE;
    }

    ReadPackageAndHash(path, type);

    return EXIT_SUCCESS;
  }

  // If single player game, the last screen the player will ever see 
  // is the game over screen so it goes to the bottom of the stack
  // before the TitleSceene:
  // g.push<GameOverScene>(); // <-- uncomment

  g.push<TitleScene>(g.Boot(results));
  return EXIT_SUCCESS;
}

int HandleBattleOnly(Game& g, TaskGroup tasks, const std::string& playerpath, const std::string& mobpath, const std::string& folderPath, bool isURL) {
  std::string mobid = mobpath;

  if (isURL) {
    auto result = DownloadPackageFromURL<ScriptedMob>(mobpath, g.MobPackagePartitioner().GetPartition(Game::LocalPartition));
    if (result.is_error()) {
      Logger::Log(LogLevel::critical, result.error_cstr());
      return EXIT_FAILURE;
    }

    mobid = result.value();
  }

  // wait for resources to be available for us
  const unsigned int maxtasks = tasks.GetTotalTasks();
  while (tasks.HasMore()) {
    const std::string taskname = tasks.GetTaskName();
    const unsigned int tasknumber = tasks.GetTaskNumber();
    Logger::Logf(LogLevel::info, "Running %s, [%i/%i]", taskname.c_str(), tasknumber+1u, maxtasks);
    tasks.DoNextTask();
  }

  ResourceHandle handle;

  // Play the pre battle rumble sound
  handle.Audio().Play(AudioType::PRE_BATTLE, AudioPriority::high);

  // Stop music and go to battle screen 
  handle.Audio().StopStream();

  auto field = std::make_shared<Field>(6, 3);

  // Get the navi we selected
  auto& playermeta = g.PlayerPackagePartitioner().GetPartition(Game::LocalPartition).FindPackageByID(playerpath);
  const std::string& image = playermeta.GetMugshotTexturePath();
  Animation mugshotAnim = Animation() << playermeta.GetMugshotAnimationPath();
  const std::string& emotionsTexture = playermeta.GetEmotionsTexturePath();
  auto mugshot = handle.Textures().LoadFromFile(image);
  auto emotions = handle.Textures().LoadFromFile(emotionsTexture);
  auto player = std::shared_ptr<Player>(playermeta.GetData());

  auto& mobmeta = g.MobPackagePartitioner().GetPartition(Game::LocalPartition).FindPackageByID(mobid);
  Mob* mob = mobmeta.GetData()->Build(field);

  // Shuffle our new folder
  std::unique_ptr<CardFolder> folder = LoadFolderFromFile(folderPath, g.CardPackagePartitioner().GetPartition(Game::LocalPartition));

  // Queue screen transition to Battle Scene with a white fade effect
  // just like the game
  if (!mob->GetBackground()) {
    mob->SetBackground(std::make_shared<ACDCBackground>());
  }

  static PA programAdvance;

  MobBattleProperties props{
    { player, programAdvance, std::move(folder), field, mob->GetBackground() },
    MobBattleProperties::RewardBehavior::take,
    { mob },
    sf::Sprite(*mugshot),
    mugshotAnim,
    emotions,
  };

  g.push<MobBattleScene>(std::move(props));
  return EXIT_SUCCESS;
}

template<typename ScriptedDataType, typename PackageManager>
stx::result_t<std::string> DownloadPackageFromURL(const std::string& url, PackageManager& packageManager)
{
  using namespace Poco::Net;
  Poco::URI uri(url);
  std::string path(uri.getPathAndQuery());
  if (path.empty()) {
    return stx::error<std::string>("`moburl` was empty. Aborting.");
  }

  HTTPClientSession session(uri.getHost(), uri.getPort());
  HTTPRequest request(HTTPRequest::HTTP_GET, path, HTTPMessage::HTTP_1_1);
  HTTPResponse response;

  session.sendRequest(request);
  std::istream& rs = session.receiveResponse(response);
  std::cout << "[Response Status] " << response.getStatus() << " " << response.getReason() << std::endl;

  if (response.getStatus() == Poco::Net::HTTPResponse::HTTP_MOVED_PERMANENTLY) {
    std::cout << "Redirect: " << response.get("Location") << std::endl;
  }

  if (response.getStatus() == Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED) {
    return stx::error<std::string>("Unable to download package. Result was HTTP_UNAUTHORIZED. Aborting.");
  }

  std::string outpath = "cache/" + stx::rand_alphanum(12) + ".zip";
  std::ofstream ofs(outpath, std::fstream::binary);
  Poco::StreamCopier::copyStream(rs, ofs);
  ofs.close();

  return packageManager.template LoadPackageFromZip<ScriptedDataType>(outpath);
}

std::unique_ptr<CardFolder> LoadFolderFromFile(const std::string& filePath, CardPackageManager& packageManager) {
  std::unique_ptr<CardFolder> folder = std::make_unique<CardFolder>();
  std::fstream file;
  file.open(filePath, std::ios::in); 
  const char space = ' ';

  if (file.is_open()) {
    std::string line;
    while (std::getline(file, line)) { 
      std::vector<std::string> tokens = stx::tokenize(line, space);

      if (tokens.size() < 2) {
        Logger::Logf(LogLevel::debug, "Card folder list needs two entries per line: `PACKAGE_ID CODE`");
        continue;
      }
      const std::string packageID = tokens[0];
      char code = tokens[1][0];
      code = std::isalpha(code) ? code : '*';

      if (packageManager.HasPackage(packageID)) {
        Battle::Card::Properties props = packageManager.FindPackageByID(packageID).GetCardProperties();
        props.code = code;
        folder->AddCard(props);
      }
    }

    file.close();
  }

  folder->Shuffle();
  return folder;
}

//!< Takes in a package manager and filters output before storing it in an output buffer `outStr` and storing the max line length for further decorating
template<typename PackageManagerT>
void FormatPackageHashOutput(PackageManagerT& pm, std::string& outStr, size_t& maxLineLen) {
  std::string first = pm.FirstValidPackage();
  std::string curr = first;

  if (first.empty()) return;

  do {
    const std::string& hash = pm.FindPackageByID(curr).GetPackageFingerprint();
    const std::string& line = hash + " " + curr;
    maxLineLen = std::max(line.size(), maxLineLen);
    outStr += line + "\n";

    curr = pm.GetPackageAfter(curr);
  } while (first != curr);
}

static std::string MakeHeader(const std::string& title, size_t len, char padChar) {
  if (len == 0) return "";

  std::string header = std::string(len, padChar);
  size_t origin = title.size() / 2;
  size_t header_origin = len / 2;
  header.replace(header_origin-origin, title.size(), title.data());
  return header;
}

template<typename PackageManagerT>
void CollectPackageHashBuffer(PackageManagerT& pm, std::string& outStr, size_t& maxLineLen) {
  std::string subOutStr;
  size_t lineLen{};

  FormatPackageHashOutput(pm, subOutStr, lineLen);
  maxLineLen = std::max(maxLineLen, lineLen);

  if (lineLen == 0) {
    lineLen = maxLineLen;
    subOutStr = MakeHeader("(empty)", lineLen, ' ') + '\n';
  }

  outStr += subOutStr;
}

void PrintPackageHash(Game& g, TaskGroup tasks) {
  // wait for resources to be available for us
  const unsigned int maxtasks = tasks.GetTotalTasks();
  while (tasks.HasMore()) {
    const std::string taskname = tasks.GetTaskName();
    const unsigned int tasknumber = tasks.GetTaskNumber();
    Logger::Logf(LogLevel::info, "Running %s, [%i/%i]", taskname.c_str(), tasknumber + 1u, maxtasks);
    tasks.DoNextTask();
  }

  BlockPackageManager& blocks = g.BlockPackagePartitioner().GetPartition(Game::LocalPartition);
  PlayerPackageManager& players = g.PlayerPackagePartitioner().GetPartition(Game::LocalPartition);
  CardPackageManager& cards = g.CardPackagePartitioner().GetPartition(Game::LocalPartition);
  MobPackageManager& mobs = g.MobPackagePartitioner().GetPartition(Game::LocalPartition);
  LuaLibraryPackageManager& libs = g.LuaLibraryPackagePartitioner().GetPartition(Game::LocalPartition);

  size_t lineLen{};
  std::string blockStr, playerStr, cardStr, mobStr, libStr;
  CollectPackageHashBuffer(blocks, blockStr, lineLen);
  CollectPackageHashBuffer(players, playerStr, lineLen);
  CollectPackageHashBuffer(cards, cardStr, lineLen);
  CollectPackageHashBuffer(mobs, mobStr, lineLen);
  CollectPackageHashBuffer(libs, libStr, lineLen);

  // print header
  std::cout << MakeHeader("HASHES", lineLen, '=') << std::endl << std::endl;

  // print each output
  std::cout << MakeHeader(typeid(BlockPackageManager).name(), lineLen, '.') << std::endl;
  std::cout << blockStr << std::endl;
  std::cout << MakeHeader(typeid(PlayerPackageManager).name(), lineLen, '.') << std::endl;
  std::cout << playerStr << std::endl;
  std::cout << MakeHeader(typeid(CardPackageManager).name(), lineLen, '.') << std::endl;
  std::cout << cardStr << std::endl;
  std::cout << MakeHeader(typeid(MobPackageManager).name(), lineLen, '.') << std::endl;
  std::cout << mobStr << std::endl;
  std::cout << MakeHeader(typeid(LuaLibraryPackageManager).name(), lineLen, '.') << std::endl;
  std::cout << libStr << std::endl;
}

template<typename ScriptedDataT, typename PackageManagerT>
void ReadPackageStep(PackageManagerT& pm, const std::string& path, std::string& id, std::string& hash) {
  stx::result_t<std::string> maybe_id = pm.template LoadPackageFromZip<ScriptedDataT>(path);

  if (maybe_id.is_error()) {
    std::cerr << maybe_id.error_cstr() << std::endl;
    return;
  }

  id = maybe_id.value();
  hash = pm.FindPackageByID(id).GetPackageFingerprint();
}

// NOTE: Game needs to instantiate before calling this function so 
//       that PackageManager's ResourceHandle variable will
//       have a valid script manager ptr!
void ReadPackageAndHash(const std::string& path, const std::string& modType) {
  BlockPackageManager blocks("dummy");
  PlayerPackageManager players("dummy");
  CardPackageManager cards("dummy");
  MobPackageManager mobs("dummy");
  LuaLibraryPackageManager libs("dummy");

  std::string id;
  std::string hash;

  if (modType == "block") {
    ReadPackageStep<ScriptedBlock>(blocks, path, id, hash);
  }

  if (modType == "player") {
    ReadPackageStep<ScriptedPlayer>(players, path, id, hash);
  }

  if (modType == "card") {
    ReadPackageStep<ScriptedCard>(cards, path, id, hash);
  }

  if (modType == "mob") {
    ReadPackageStep<ScriptedMob>(mobs, path, id, hash);
  }


  if (modType == "lib") {
    ReadPackageStep<LuaLibrary>(libs, path, id, hash);
  }

  if (id.empty() || hash.empty()) {
    std::cerr << "Not a valid mod type `" << modType << "`" << std::endl;
    return;
  }

  std::cout << hash << " " << id;
}