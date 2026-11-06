#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace soui::benchmark::rejected_frame {
constexpr unsigned width=64, height=36, packed_pitch=width*4;
using pixels=std::array<unsigned char,packed_pitch*height>;
struct sample {
  pixels bgra{};
  std::array<unsigned char,16> marker{};
  unsigned ambiguous{};
  std::uint16_t stored_crc{}, calculated_crc{};
};
inline std::uint16_t crc(const std::array<unsigned char,16>& bytes) {
  std::uint16_t result=0xffff;
  for(unsigned i=0;i<bytes.size();++i) {
    if(i==12||i==13)continue;
    result^=static_cast<std::uint16_t>(bytes[i])<<8;
    for(unsigned bit=0;bit<8;++bit)result=static_cast<std::uint16_t>((result<<1)^((result&0x8000)?0x1021:0));
  }
  return result;
}
inline sample inspect(const void* data,unsigned row_pitch) {
  if(!data||row_pitch<packed_pitch)throw std::runtime_error("Rejected frame buffer invalid");
  sample result;
  for(unsigned y=0;y<height;++y)std::memcpy(result.bgra.data()+y*packed_pitch,static_cast<const unsigned char*>(data)+y*row_pitch,packed_pitch);
  for(unsigned i=0;i<128;++i) {
    const auto* p=result.bgra.data()+((i/16)*4+2)*packed_pitch+((i%16)*4+2)*4;
    const bool dark=p[0]<=32&&p[1]<=32&&p[2]<=32;
    const bool light=p[0]>=223&&p[1]>=223&&p[2]>=223;
    if(!dark&&!light)++result.ambiguous;
    if(light)result.marker[i/8]|=static_cast<unsigned char>(1U<<(i%8));
  }
  result.stored_crc=static_cast<std::uint16_t>(result.marker[12]|(static_cast<unsigned>(result.marker[13])<<8));
  result.calculated_crc=crc(result.marker);
  return result;
}
inline std::string hex(const std::array<unsigned char,16>& bytes) {
  std::string result;result.reserve(32);constexpr char digits[]="0123456789abcdef";
  for(auto b:bytes){result+=digits[b>>4];result+=digits[b&15];}return result;
}
inline std::string json_string(const std::string& value) {
  std::string result="\"";constexpr char digits[]="0123456789abcdef";
  for(unsigned char c:value) {
    if(c=='\\'||c=='\"'){result+='\\';result+=static_cast<char>(c);}
    else if(c<32){result+="\\u00";result+=digits[c>>4];result+=digits[c&15];}
    else result+=static_cast<char>(c);
  }
  return result+'\"';
}
inline void save_new(const std::filesystem::path& path,const pixels& bytes) {
  const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create rejected frame artifact: win32="+std::to_string(GetLastError()));
  struct closer {HANDLE value;~closer(){CloseHandle(value);}} close{file};
  DWORD written{};
  if(!WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(file))
    throw std::runtime_error("Cannot persist complete rejected frame artifact");
}
inline std::string metadata(const sample& value,const std::string& basename,const std::string& reason,
    std::uint64_t present,std::uint64_t observed,bool ready) {
  return "{\"event\":\"desktop.reject\",\"reason\":"+json_string(reason)+
    ",\"lastPresentQpc\":\""+std::to_string(present)+"\",\"observedQpc\":\""+std::to_string(observed)+
    "\",\"previouslyReady\":"+(ready?"true":"false")+",\"width\":64,\"height\":36,\"format\":\"BGRA8\",\"rowPitch\":256,\"rawBytes\":9216,\"rawPath\":"+
    json_string(basename)+",\"markerHex\":"+json_string(hex(value.marker))+",\"ambiguousPixels\":"+std::to_string(value.ambiguous)+
    ",\"storedCrc\":"+std::to_string(value.stored_crc)+",\"calculatedCrc\":"+std::to_string(value.calculated_crc)+"}";
}
}
