// Zeal named_pipe.cpp protocol port; transport uses owned overlapped buffers
// polled on the game thread, with bounded queues and no detached worker.
#include "gusto/client/rof2/named_pipe.hpp"
#include "gusto/third_party/json.hpp"
#include <algorithm>
namespace gusto::client::rof2 {
std::string PipeEnvelope(PipeType type,std::string data,std::string character){
 return nlohmann::json{{"type",int(type)},{"data_len",data.size()},{"data",data},{"character",character}}.dump(-1,' ',false,nlohmann::json::error_handler_t::replace)+"\n";
}
NamedPipeServer::Connection::~Connection(){
 if(pipe!=INVALID_HANDLE_VALUE){
  // Cancellation must complete before OVERLAPPED and its string buffer die.
  if(pending){CancelIo(pipe);DWORD transferred=0;GetOverlappedResult(pipe,&io,&transferred,TRUE);}
  DisconnectNamedPipe(pipe);CloseHandle(pipe);
 }
 if(event)CloseHandle(event);
}
bool NamedPipeServer::Listen(){
 if(connections.size()>=5)return true; // four consumers plus one listener
 auto c=std::make_unique<Connection>();
 c->event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!c->event){error=GetLastError();return false;}
 c->io.hEvent=c->event;
 c->pipe=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_OUTBOUND|FILE_FLAG_OVERLAPPED,
  PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,5,32768,0,0,nullptr);
 if(c->pipe==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
 if(ConnectNamedPipe(c->pipe,&c->io))c->connecting=false;
 else {DWORD e=GetLastError();if(e==ERROR_IO_PENDING)c->pending=true;else if(e==ERROR_PIPE_CONNECTED)c->connecting=false;else{error=e;return false;}}
 connections.push_back(std::move(c));return true;
}
bool NamedPipeServer::Start(std::wstring endpoint){Stop();error=0;drops=0;name=std::move(endpoint);return Listen();}
void NamedPipeServer::Stop(){connections.clear();name.clear();}
std::size_t NamedPipeServer::Clients() const{return std::count_if(connections.begin(),connections.end(),[](const auto& c){return !c->connecting;});}
void NamedPipeServer::Tick(){
 if(name.empty())return;
 for(auto it=connections.begin();it!=connections.end();){
  auto& c=**it;bool remove=false;
  if(c.pending){
   DWORD transferred=0;
   if(GetOverlappedResult(c.pipe,&c.io,&transferred,FALSE)){
    c.pending=false;
    if(c.connecting)c.connecting=false;
    else {if(transferred!=c.active.size())remove=true;c.bytes-=c.active.size();c.active.clear();}
   } else if(GetLastError()!=ERROR_IO_INCOMPLETE){error=GetLastError();remove=true;}
  }
  for(int budget=0;budget<16 && !remove && !c.connecting && !c.pending && !c.queued.empty();++budget){
   c.active=std::move(c.queued.front());c.queued.pop_front();ResetEvent(c.event);c.io={};c.io.hEvent=c.event;
   DWORD sent=0;
   if(WriteFile(c.pipe,c.active.data(),DWORD(c.active.size()),&sent,&c.io)){
    if(sent!=c.active.size())remove=true;c.bytes-=c.active.size();c.active.clear();
   }else if(GetLastError()==ERROR_IO_PENDING)c.pending=true;
   else {error=GetLastError();remove=true;}
  }
  if(remove)it=connections.erase(it);else ++it;
 }
 bool listening=std::any_of(connections.begin(),connections.end(),[](const auto& c){return c->connecting;});
 if(!listening && Clients()<4)Listen();
}
void NamedPipeServer::Publish(std::string data){
 if(data.empty() || data.size()>65536){++drops;return;}
 for(auto it=connections.begin();it!=connections.end();){
  auto& c=**it;if(c.connecting){++it;continue;}
  // Disconnect slow readers instead of freezing gameplay or retaining stale data.
  if(c.queued.size()>=64 || c.bytes+data.size()>262144){++drops;it=connections.erase(it);continue;}
  c.bytes+=data.size();c.queued.push_back(data);++it;
 }
}
}
