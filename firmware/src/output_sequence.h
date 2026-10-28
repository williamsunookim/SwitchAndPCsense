// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
class OutputSequence {
public:
 void stamp(uint8_t *data,size_t size) {
  if (size<2 || (data[0]!=0x31 && data[0]!=0x32 && data[0]!=0x36)) return;
  data[1]=static_cast<uint8_t>((data[1]&15)|((next_++&15)<<4));
 }
private:uint8_t next_=0;
};
