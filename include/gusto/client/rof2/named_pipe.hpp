#pragma once
#include "gusto/client/rof2/compatibility.hpp"
#include <windows.h>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace gusto::client::rof2 {
// Zeal's outbound JSON envelope and type numbering are retained.
enum class PipeType {Log,Label,Gauge,Player,Custom,Raid,Group};
std::string PipeEnvelope(PipeType,std::string data,std::string character);
class NamedPipeServer {
 struct Connection {
  HANDLE pipe=INVALID_HANDLE_VALUE,event=nullptr;OVERLAPPED io{};
  bool connecting=true,pending=false;std::string active;
  std::deque<std::string> queued;std::size_t bytes=0;
  ~Connection();
 };
 std::wstring name;std::vector<std::unique_ptr<Connection>> connections;
 DWORD error=0;std::size_t drops=0;
 bool Listen();
 public:
 bool Start(std::wstring endpoint);
 void Stop();
 void Tick();
 void Publish(std::string data);
 std::size_t Clients() const;
 DWORD Error() const{return error;}
 std::size_t Drops() const{return drops;}
 ~NamedPipeServer(){Stop();}
};
struct PipeOptions {bool enabled=true,verbose=false;int delay=100;bool zealAlias=false;};
bool InitializePipes(HMODULE,const ValidatedClient&);
PipeOptions GetPipeOptions();
void SetPipeOptions(PipeOptions);
void LoadPipes(const wchar_t*);
void SavePipes(const wchar_t*);
void TickPipes();
void ShutdownPipes();
void PipeChat(const char* message,int color);
bool HandlePipeCommand(std::string_view,std::string& reply);
std::string PipeStatus();
}
