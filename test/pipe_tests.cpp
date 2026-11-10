#include "gusto/client/rof2/named_pipe.hpp"
#include "gusto/third_party/json.hpp"
#include <cstdio>
#include <cstdlib>
using namespace gusto::client::rof2;
int checks=0;
void Check(bool value,const char* why){++checks;if(!value){std::printf("FAIL pipe %d: %s (error %lu)\n",checks,why,GetLastError());std::exit(1);}}
HANDLE Open(const std::wstring& name,NamedPipeServer& server){server.Tick();auto client=CreateFileW(name.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);Check(client!=INVALID_HANDLE_VALUE,"connect consumer");for(int i=0;i<10;++i){server.Tick();Sleep(1);}return client;}
std::string ReadLine(HANDLE client,NamedPipeServer& server){
 std::string result;auto deadline=GetTickCount64()+3000;
 while(GetTickCount64()<deadline && result.find('\n')==std::string::npos){
  char buffer[1024]{};OVERLAPPED io{};io.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);DWORD got=0;
  bool pending=false;BOOL ok=ReadFile(client,buffer,sizeof(buffer),&got,&io);
  if(!ok){Check(GetLastError()==ERROR_IO_PENDING,"consumer read starts");pending=true;
   while(GetTickCount64()<deadline){server.Tick();if(GetOverlappedResult(client,&io,&got,FALSE)){pending=false;ok=TRUE;break;}if(GetLastError()!=ERROR_IO_INCOMPLETE)break;Sleep(1);}
  }
  if(pending){CancelIo(client);GetOverlappedResult(client,&io,&got,TRUE);}CloseHandle(io.hEvent);
  Check(ok && got,"consumer read completes");result.append(buffer,got);
 }
 Check(result.find('\n')!=std::string::npos,"newline frame assembled");return result;
}
int main(){
 NamedPipeServer server;auto name=L"\\\\.\\pipe\\gusto_test_"+std::to_wstring(GetCurrentProcessId());
 Check(server.Start(name),"start listener");Check(!server.Clients(),"no clients initially");
 HANDLE a=Open(name,server),b=Open(name,server);Check(server.Clients()==2,"two concurrent consumers");
 auto packet=PipeEnvelope(PipeType::Custom,"{\"text\":\"hello \\\"ghost\\\"\"}","Aggatha");server.Publish(packet);
 auto first=ReadLine(a,server),second=ReadLine(b,server);Check(first==packet && second==packet,"broadcast identical bytes");
 auto parsed=nlohmann::json::parse(first);Check(parsed["type"]==4 && parsed["character"]=="Aggatha" && parsed["data_len"]==parsed["data"].get<std::string>().size(),"Zeal envelope lengths and values");
 std::string big(48000,'x');auto large=PipeEnvelope(PipeType::Custom,big,"Aggatha");server.Publish(large);
 Check(ReadLine(a,server)==large && ReadLine(b,server)==large,"overlapped large writes retain buffers across fragmented reads");
 CloseHandle(a);CloseHandle(b);server.Publish(packet);for(int i=0;i<12;++i){server.Tick();Sleep(1);}Check(server.Clients()==0,"broken consumers removed");
 HANDLE c=Open(name,server);Check(server.Clients()==1,"consumer reconnects");
 server.Publish(large);server.Tick(); // leave an overlapped write outstanding
 for(int i=0;i<80;++i)server.Publish(large);
 Check(server.Drops()>0 && server.Clients()==0,"slow consumer disconnected with bounded queued bytes");CloseHandle(c);
 server.Tick();HANDLE d=Open(name,server);server.Publish(large);server.Tick();auto before=GetTickCount64();server.Stop();Check(GetTickCount64()-before<2000,"shutdown cancels and drains pending I/O");CloseHandle(d);
 Check(server.Clients()==0,"shutdown closes consumers and listener");Check(server.Start(name),"restart listener");server.Stop();
 std::printf("PASS: %d named-pipe checks (multi-client, JSON, large async writes, reconnect, bounded queues and cancellation)\n",checks);
}
