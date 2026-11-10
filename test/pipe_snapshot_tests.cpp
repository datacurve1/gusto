// Exercise the actual service fanout and commands without authorizing EQ memory.
#include "../src/client/rof2/pipe_snapshot.cpp"
#include <cstdio>
#include <cstdlib>
namespace gusto::client::rof2 {
const ClientState* GetClientState(){return nullptr;}
InitializationStatus GetInitializationStatus(){return InitializationStatus::Ready;}
void* ResolveObject(HMODULE,const ValidatedClient&,const addresses::Global&,std::size_t){return nullptr;}
PlayerClient* ClientState::GetLocalPlayer() const{return nullptr;}
PlayerClient* ClientState::GetTarget() const{return nullptr;}
bool ClientState::GetZoneSnapshot(ZoneSnapshot&) const{return false;}
void ReadHudLocalResources(int&,int&,int&,int&){}
}
using namespace gusto::client::rof2;
int checks=0;
void Check(bool good){if(!good){std::printf("FAIL pipe service check %d\n",checks+1);std::exit(1);}++checks;}
HANDLE Open(const std::wstring& name,NamedPipeServer& s){s.Tick();auto h=CreateFileW(name.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);Check(h!=INVALID_HANDLE_VALUE);s.Tick();return h;}
std::string ReadPacket(HANDLE h){std::string out;auto deadline=GetTickCount64()+1000;
 while(GetTickCount64()<deadline && out.find('\n')==out.npos){server.Tick();zealServer.Tick();DWORD count=0;if(PeekNamedPipe(h,nullptr,0,nullptr,&count,nullptr)&&count){char buffer[8192];DWORD got=0;Check(ReadFile(h,buffer,std::min<DWORD>(count,sizeof(buffer)),&got,nullptr)!=0);out.append(buffer,got);}else Sleep(1);}
 Check(out.find('\n')!=out.npos);return out;
}
int main(){
 auto suffix=std::to_wstring(GetCurrentProcessId());auto a=L"\\\\.\\pipe\\gusto_service_test_"+suffix,b=L"\\\\.\\pipe\\zeal_service_test_"+suffix;
 options={true,false,100,true};character="Aggatha";
 Check(server.Start(a));Check(zealServer.Start(b));started=zealStarted=true;
 auto first=Open(a,server),second=Open(b,zealServer);
 Publish(PipeType::Custom,"{\"text\":\"hello\"}");auto packet=ReadPacket(first);Check(ReadPacket(second)==packet);
 auto json=nlohmann::json::parse(packet);Check(json["character"]=="Aggatha" && json["type"]==4);
 PipeChat("Hello\nWorld",258);packet=ReadPacket(first);Check(ReadPacket(second)==packet);json=nlohmann::json::parse(packet);Check(nlohmann::json::parse(json["data"].get<std::string>())["text"]=="HelloWorld");
 std::string reply;Check(HandlePipeCommand("/gusto pipes zeal off",reply));Check(!GetPipeOptions().zealAlias && zealServer.Clients()==0 && server.Clients()==1);
 Publish(PipeType::Custom,"{}");Check(!ReadPacket(first).empty());
 Check(HandlePipeCommand("/gusto pipes zeal on",reply));Check(GetPipeOptions().zealAlias);
 ShutdownPipes();Check(!server.Clients() && !zealServer.Clients() && character.empty());CloseHandle(first);CloseHandle(second);
 Check(!InitializePipes(nullptr,{}));TickPipes();Check(!started && !zealStarted);
 std::printf("PASS: %d pipe service checks (actual Gusto/Zeal fanout, chat, alias toggle, shutdown and validation gate)\n",checks);
}
