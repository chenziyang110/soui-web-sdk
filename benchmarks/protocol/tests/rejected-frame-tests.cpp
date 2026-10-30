#include "../rejected-frame-evidence.h"
#include <fstream>
#include <iostream>
#include <vector>

namespace rf=soui::benchmark::rejected_frame;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class Fn>void fails(Fn fn,const char* message){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed,message);}
int wmain(int argc,wchar_t** argv) {
 try{
  check(argc==2,"Usage: RejectedFrameTests NEW_DIRECTORY");
  const std::filesystem::path root(argv[1]);check(!std::filesystem::exists(root),"Directory must be new");std::filesystem::create_directory(root);
  std::array<unsigned char,16> marker{0xa7,0x53,1,2,3,4,194,0,0,80,0,1,0,0,194,0};
  const auto crc=rf::crc(marker);marker[12]=static_cast<unsigned char>(crc);marker[13]=static_cast<unsigned char>(crc>>8);
  constexpr unsigned pitch=rf::packed_pitch+37;
  std::vector<unsigned char> padded(pitch*rf::height,0x5a);
  for(unsigned y=0;y<rf::height;++y)for(unsigned x=0;x<rf::packed_pitch;++x)padded[y*pitch+x]=0;
  for(unsigned i=0;i<128;++i){auto* pixel=padded.data()+((i/16)*4+2)*pitch+((i%16)*4+2)*4;pixel[0]=pixel[1]=pixel[2]=(marker[i/8]&(1U<<(i%8)))?255:0;pixel[3]=255;}
  auto valid=rf::inspect(padded.data(),pitch);check(valid.marker==marker&&valid.ambiguous==0&&valid.stored_crc==valid.calculated_crc,"valid marker decode mismatch");
  for(unsigned y=0;y<rf::height;++y)check(std::memcmp(valid.bgra.data()+y*rf::packed_pitch,padded.data()+y*pitch,rf::packed_pitch)==0,"padded RowPitch packing mismatch");
  // Corrupt one CRC-bit pixel without changing payload; injection exists only in this fixture.
  const unsigned bit=12*8;auto* pixel=padded.data()+((bit/16)*4+2)*pitch+((bit%16)*4+2)*4;
  pixel[0]=pixel[1]=pixel[2]=pixel[0]?0:255;
  auto corrupt=rf::inspect(padded.data(),pitch);check(corrupt.ambiguous==0&&corrupt.stored_crc!=corrupt.calculated_crc,"corrupt CRC not diagnosed");
  const auto raw=root/L"fixture.reject.bgra";rf::save_new(raw,corrupt.bgra);
  std::ifstream input(raw,std::ios::binary);std::vector<unsigned char> read((std::istreambuf_iterator<char>(input)),{});
  check(read.size()==corrupt.bgra.size()&&std::memcmp(read.data(),corrupt.bgra.data(),read.size())==0,"artifact roundtrip differs");
  fails([&]{rf::save_new(raw,valid.bgra);},"existing evidence overwritten");
  check(std::filesystem::file_size(raw)==rf::pixels{}.size(),"existing evidence length changed");
  std::ifstream after(raw,std::ios::binary);std::vector<unsigned char> unchanged((std::istreambuf_iterator<char>(after)),{});check(unchanged==read,"existing evidence bytes changed");
  fails([&]{rf::save_new(root/L"missing"/L"x.bgra",valid.bgra);},"invalid destination accepted");
  pixel[0]=pixel[1]=pixel[2]=128;auto ambiguous=rf::inspect(padded.data(),pitch);check(ambiguous.ambiguous==1,"ambiguity not counted");
  fails([&]{rf::inspect(nullptr,pitch);},"null buffer accepted");fails([&]{rf::inspect(padded.data(),255);},"short RowPitch accepted");
  const auto metadata=rf::metadata(corrupt,"fixture.reject.bgra","CRC invalid",123,456,true);
  check(metadata.find("\"event\":\"desktop.reject\"")!=std::string::npos&&metadata.find("\"rawBytes\":9216")!=std::string::npos&&metadata.find(rf::hex(corrupt.marker))!=std::string::npos,"metadata incomplete");
  check(rf::json_string("a\n\\\"")=="\"a\\u000a\\\\\\\"\"","JSON escaping differs");
  std::ofstream(root/L"metadata.json",std::ios::binary)<<metadata<<'\n';
  std::cout<<"PASS valid, CRC-corrupt, ambiguity, RowPitch, roundtrip, no-overwrite, invalid destination, invalid buffer, metadata\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
