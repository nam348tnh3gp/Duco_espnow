#ifndef _COUNTER_H_
#define _COUNTER_H_

#include <Arduino.h>
#include <string.h>

template <unsigned int max_digits>
class Counter {

public:
  Counter() { reset(); }

  void reset() {
    memset(buffer, '0', max_digits);
    buffer[max_digits] = '\0';
    val = 0;
    len = 1;
  }

  inline Counter &operator++() {
    inc_string(buffer + max_digits - 1);
    ++val;
    return *this;
  }

  inline operator unsigned int() const { return val; }
  inline const char *c_str() const { return buffer + max_digits - len; }
  inline size_t strlen() const { return len; }
  
  // Thêm method để lấy giá trị
  inline unsigned int getVal() const { return val; }

protected:
  inline void inc_string(char *c) {
    if (*c < '9') {
      *c += 1;
    }
    else {
      *c = '0';
      inc_string(c - 1);
      len = max(max_digits - (c - buffer) + 1, len);
    }
  }

protected:
  char buffer[max_digits + 1];
  unsigned int val;
  size_t len;
};

#endif
