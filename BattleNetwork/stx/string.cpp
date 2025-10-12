#include "string.h"
#include <sstream>
#include <charconv>
#include <iomanip>
#include <map>

// NOTE: the following code was from http://burtleburtle.net/bob/c/lookup3.c
// References: http://burtleburtle.net/bob/hash/index.html
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

#define mix(a,b,c) \
{ \
  a -= c;  a ^= rot(c, 4);  c += b; \
  b -= a;  b ^= rot(a, 6);  a += c; \
  c -= b;  c ^= rot(b, 8);  b += a; \
  a -= c;  a ^= rot(c,16);  c += b; \
  b -= a;  b ^= rot(a,19);  a += c; \
  c -= b;  c ^= rot(b, 4);  b += a; \
}

#define final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}

namespace stx {
  std::string replace(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
      str.replace(start_pos, from.length(), to);
      start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
    return str;
  }

  std::vector<std::string> tokenize(const std::string& str, char delim) {
    std::vector<std::string> res;

    std::istringstream f(str);
    std::string s;
    while (std::getline(f, s, delim)) {
      res.push_back(s);
    }

    return  res;
  }

  bool insensitive_equals(const std::string_view& a, const std::string_view& b) {
    if (a.size() != b.size()) {
      return false;
    }

    for (size_t i = 0; i < a.size(); i++) {
      if (toupper(a[i]) != toupper(b[i])) {
        return false;
      }
    }

    return true;
  }

  std::string rand_alphanum(size_t n)
  {
    std::vector<char> letters = {
      'a','b','c','d','e','f','g','h','i','j', 'k','l','m',
      'n','o','p','q', 'r','s','t','u','v','w','x', 'y','z',
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };

    std::string str;
    for (size_t i = 0; i < n; i++) {
      str += letters[rand() % letters.size()];
    }

    return str;
  }
  stx::result_t<int> to_int(const std::string& str)
  {
    int result{};

    auto [ptr, ec] { std::from_chars(str.data(), str.data() + str.size(), result) };

    if (ec == std::errc::invalid_argument)
    {
      return error<int>("Input is not an int");
    }
    else if (ec == std::errc::result_out_of_range)
    {
      return error<int>("Input number is larger than an int");
    }

    // if (ec == std::errc())
    return ok(result);
  }

  stx::result_t<float> to_float(const std::string& str)
  {
#ifdef __unix__
    // from_chars for floats isn't supported on many systems yet, using strtof outside of windows
    try {
      return std::strtof(str.c_str(), nullptr);
    } catch (std::exception& e) {
      return error<float>(e.what());
    }
#else
    float result{};

    auto [ptr, ec] { std::from_chars(str.data(), str.data() + str.size(), result) };

    if (ec == std::errc::invalid_argument)
    {
      return error<float>("Input is not a float");
    }
    else if (ec == std::errc::result_out_of_range)
    {
      return error<float>("Input number is larger than a float");
    }

    return ok(result);
#endif
  }
  std::string format_to_fit(const std::string& str, size_t max_cols, size_t max_rows)
  {
    if (str.empty()) return str;

    std::string output = str;
    static const std::string escaped_newline = "\\n";
    static const std::string newline = "\n";
    static const std::string space = " ";

    output = stx::replace(output, escaped_newline, space);
    output = stx::replace(output, newline, space);
    output = stx::replace(output, space + space, space);

    std::vector<std::string> words = stx::tokenize(output, space.c_str()[0]);

    output.clear();
    size_t row_count = 0; // track total row count
    size_t line_len = 0; // track current line length
    auto check_line = [&line_len, &output, &row_count, max_rows, max_cols](const std::string& str) {
      if (line_len + str.size() > max_cols) {
        output += "\n";
        row_count++;
        line_len = 0;

        if (row_count == max_rows) return;
      }

      output += str;
      line_len += str.size();
    };

    for (std::string& w : words) {
      size_t word_len = w.size();
      std::vector<std::string> word_pieces;

      size_t last_letter = 0;
      for (size_t i = 0; i <= word_len; i++) {
        bool break_here = (i == word_len) || (i % max_cols == 0 && i > 0);

        if (!break_here) continue;

        word_pieces.push_back(w.substr(last_letter, (i - last_letter)));
        last_letter = i;
      }

      // we will always have at least one word
      for (std::string& p : word_pieces) {
        check_line(p);
      }

      if (output[output.size() - 1] != '\n' && line_len + 1 < max_cols) {
        check_line(space);
      }

      // quit early to prevent breaking formatting
      if (row_count == max_rows) {
        break;
      }
    }

    output.resize(std::min(output.size(), (size_t)(max_cols * max_rows)));
    output.shrink_to_fit();

    return output;
  }

  std::string as_hex(const std::string& buffer, size_t stride) {
    std::stringstream ssout;

    for (size_t i = 0; i < buffer.size(); i++) {
      unsigned char u = static_cast<unsigned char>(buffer[i]);
      ssout << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(u);

      if (stride > 0 && (i+1) % stride == 0) {
        ssout << " ";
      }
    }

    return ssout.str();
  }

  uint32_t hash(const std::string& str) {
    static std::map<std::string, uint32_t> cache;

    if (cache.count(str) > 0) {
      return cache[str];
    }

    uint32_t sig = hashword((uint32_t*)str.c_str(), str.length(), 0);
    cache.insert_or_assign(str, sig);
    return sig;
  }

  uint32_t hashword(
    const uint32_t* k,                   /* the key, an array of uint32_t values */
    size_t          length,               /* the length of the key, in uint32_ts */
    uint32_t        initval)         /* the previous hash, or an arbitrary value */
  {
    uint32_t a, b, c;

    /* Set up the internal state */
    a = b = c = 0xdeadbeef + (((uint32_t)length) << 2) + initval;

    /*------------------------------------------------- handle most of the key */
    while (length > 3)
    {
      a += k[0];
      b += k[1];
      c += k[2];
      mix(a, b, c);
      length -= 3;
      k += 3;
    }

    /*------------------------------------------- handle the last 3 uint32_t's */
    switch (length)                     /* all the case statements fall through */
    {
    case 3: c += k[2];
    case 2: b += k[1];
    case 1: a += k[0];
      final(a, b, c);
    case 0:     /* case 0: nothing left to add */
      break;
    }
    /*------------------------------------------------------ report the result */
    return c;
  }
}