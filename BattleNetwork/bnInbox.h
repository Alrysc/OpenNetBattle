#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <functional>
#include <stdint.h>
#include "bnAnimation.h"
#include "bnCallback.h"

class Inbox {
public:
  enum class Icons : uint8_t {
    announcement = 0,
    dm,
    dm_w_attachment,
    important,
    mission,
    size // For counting only!
  };

  struct Mail;
  using OnMailReadCallback = Callback<void(Mail&)>;
  struct Mail {
    std::string id;
    Icons icon{};
    std::string title;
    std::string from;
    std::string body;
    std::shared_ptr<sf::Texture> mugshot;
    Animation mugshotAnim;
    OnMailReadCallback onReadCallback;
    bool read{};
  };

  void AddMail(const Mail& msg);
  void RemoveMail(const std::string& id);
  void ReadMail(size_t index, std::function<void(const Mail& msg)> onRead);
  const Mail& GetAt(size_t index) const;
  void Clear();
  size_t Size() const;
  bool Empty() const;
private:
  std::vector<Mail> mailList;
};