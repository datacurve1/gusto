#include "gusto/client/rof2/named_pipe.hpp"
#include "gusto/client/rof2/pipe_model.hpp"
#include "gusto/client/rof2/hud_overlay.hpp"
#include "gusto/client/rof2/initialization.hpp"
#include "gusto/client/rof2/hook_utils.hpp"
#include "gusto/client/rof2/globals.hpp"
#include "gusto/client/rof2/addresses.hpp"
#include "gusto/third_party/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
namespace gusto::client::rof2 {
namespace {
HMODULE module=nullptr;ValidatedClient validation;
NamedPipeServer server,zealServer;PipeOptions options;bool started=false,zealStarted=false;
std::string character;std::wstring settings;ULONGLONG lastOutput=0;
template<class T> bool Read(const void* ptr,std::size_t offset,T& out){
 auto address=reinterpret_cast<std::uintptr_t>(ptr);if(!address || offset>UINTPTR_MAX-address)return false;
 SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address+offset),&out,sizeof(T),&got)&&got==sizeof(T);
}
void* VA(std::uintptr_t value,std::size_t bytes=4){return ResolveObject(module,validation,{value,addresses::Storage::DirectObject},bytes);}
void* Global(std::uintptr_t value){void* ptr=nullptr;Read(VA(value),0,ptr);return ptr;}
std::string Utf8(std::string_view text){
 if(text.empty())return {};
 int length=MultiByteToWideChar(CP_ACP,0,text.data(),int(text.size()),nullptr,0);if(length<=0)return {};
 std::wstring wide(length,L'\0');MultiByteToWideChar(CP_ACP,0,text.data(),int(text.size()),wide.data(),length);
 length=WideCharToMultiByte(CP_UTF8,0,wide.data(),int(wide.size()),nullptr,0,nullptr,nullptr);std::string result(length,'\0');
 WideCharToMultiByte(CP_UTF8,0,wide.data(),int(wide.size()),result.data(),length,nullptr,nullptr);return result;
}
std::string Name(const void* ptr,std::size_t off){std::array<char,64> text{};if(!Read(ptr,off,text))return {};text.back()=0;return Utf8(text.data());}
std::optional<PipeEntity> Entity(void* ptr){
 PipeEntity e;if(!Read(ptr,0x148,e.id)||!e.id)return {};
 e.name=Name(ptr,0xa4);Read(ptr,0x2e4,e.hp);Read(ptr,0x2dc,e.maxHP);
 BYTE byte=0;if(Read(ptr,0x250,byte))e.level=byte;if(Read(ptr,0x125,byte))e.type=byte;
 if(Read(ptr,0xeb8,byte))e.cls=byte;Read(ptr,0xeb4,e.race);
 e.position=Read(ptr,0x68,e.x)&&Read(ptr,0x64,e.y)&&Read(ptr,0x6c,e.z)&&Read(ptr,0x80,e.heading)&&std::isfinite(e.x)&&std::isfinite(e.y)&&std::isfinite(e.z)&&std::isfinite(e.heading);
 return e;
}
void Publish(PipeType type,const std::string& data){auto packet=PipeEnvelope(type,data,character);server.Publish(packet);if(options.zealAlias)zealServer.Publish(packet);}
void Snapshot(){
 const auto* state=GetClientState();ZoneSnapshot zone;
 if(!state || !state->GetZoneSnapshot(zone))return;
 auto self=Entity(state->GetLocalPlayer());if(!self)return;
 PipeSnapshot s;s.self=*self;s.zone=zone.zoneIdValid?zone.zoneId:0;character=s.self.name;
 BYTE attacking=0;Read(VA(0xddf618,0x83f9c),0x83f92,attacking);s.autoattack=attacking!=0;
 s.target=Entity(state->GetTarget());if(s.target){auto display=Name(state->GetTarget(),0xe4);if(!display.empty())s.target->name=display;}Read(state->GetLocalPlayer(),0x2b4,s.pet);
 if(s.pet==0xffffffffu)s.pet=0;
 void* group=nullptr;Read(Global(0xdd261c),0x31cc,group);
 for(int i=0;i<6;++i){void *member=nullptr,*spawn=nullptr;
  if(Read(group,4+i*4,member)&&Read(member,0x28,spawn))if(auto e=Entity(spawn);e&&e->id!=s.self.id)s.group.push_back(*e);
 }
 ReadHudLocalResources(s.mana,s.maxMana,s.endurance,s.maxEndurance);
 std::vector<PipeRaidMember> roster;auto raid=VA(0xdd2690,0x3964);
 for(int i=0;i<72;++i){BYTE used=0;if(!Read(raid,0x218+i,used)||!used)continue;
  std::size_t off=0x260+i*0x94;PipeRaidMember m;m.entity.name=Name(raid,off);if(m.entity.name.empty())continue;
  BYTE level=0,cls=0,leader=0,groupLeader=0;
  if(Read(raid,off+0x80,level))m.entity.level=level;if(Read(raid,off+0x81,cls))m.entity.cls=cls;
  Read(raid,off+0x82,leader);Read(raid,off+0x83,groupLeader);Read(raid,off+0x88,m.group);
  m.rank=leader?"Raid Leader":groupLeader?"Group Leader":"";roster.push_back(m);
 }
 // Resolve pet and raid members by walking only while a consumer needs a snapshot.
 std::unordered_map<std::string,PipeEntity> byName;std::unordered_set<void*> seen;
 void* spawn=nullptr;if(s.pet || !roster.empty())Read(Global(0xe641d0),8,spawn);
 while(spawn && seen.size()<8192 && seen.insert(spawn).second){
  if(auto e=Entity(spawn)){if(e->id==s.pet)s.petEntity=*e;if(!roster.empty())byName.emplace(e->name,*e);}
  void* next=nullptr;if(!Read(spawn,8,next))break;spawn=next;
 }
 for(auto& m:roster){if(auto found=byName.find(m.entity.name);found!=byName.end())m.entity=found->second;s.raid.push_back(m);}
 for(const auto& [type,data]:BuildPipeMessages(s,options.verbose))Publish(static_cast<PipeType>(type),data);
}
}
bool InitializePipes(HMODULE game,const ValidatedClient& token){if(!token.IsValidFor(game))return false;module=game;validation=token;return true;}
PipeOptions GetPipeOptions(){return options;}
void SetPipeOptions(PipeOptions value){value.delay=value.delay==0?0:std::clamp(value.delay,25,5000);options=value;if(!value.zealAlias){zealServer.Stop();zealStarted=false;}if(!value.enabled){server.Stop();zealServer.Stop();started=zealStarted=false;character.clear();}if(!settings.empty())SavePipes(settings.c_str());}
void LoadPipes(const wchar_t* path){settings=path;PipeOptions value;value.enabled=GetPrivateProfileIntW(L"Pipes",L"Enabled",1,path)!=0;value.verbose=GetPrivateProfileIntW(L"Pipes",L"Verbose",0,path)!=0;value.zealAlias=GetPrivateProfileIntW(L"Pipes",L"ZealAlias",0,path)!=0;value.delay=int(GetPrivateProfileIntW(L"Pipes",L"Delay",100,path));value.delay=value.delay==0?0:std::clamp(value.delay,25,5000);options=value;}
void SavePipes(const wchar_t* path){auto put=[&](const wchar_t* key,int value){auto text=std::to_wstring(value);WritePrivateProfileStringW(L"Pipes",key,text.c_str(),path);};put(L"ZealAlias",options.zealAlias);put(L"Enabled",options.enabled);put(L"Verbose",options.verbose);put(L"Delay",options.delay);}
void TickPipes(){
 if(!options.enabled || !validation.IsValidFor(module) || GetInitializationStatus()!=InitializationStatus::Ready)return;
 try{
  if(!started)started=server.Start(L"\\\\.\\pipe\\gusto_"+std::to_wstring(GetCurrentProcessId()));
  if(!started)return;server.Tick();
  if(options.zealAlias && !zealStarted)zealStarted=zealServer.Start(L"\\\\.\\pipe\\zeal_"+std::to_wstring(GetCurrentProcessId()));
  if(zealStarted)zealServer.Tick();
  auto now=GetTickCount64();
  if((server.Clients()+zealServer.Clients()) && options.delay>0 && now-lastOutput>=std::uint64_t(options.delay)){lastOutput=now;Snapshot();server.Tick();if(zealStarted)zealServer.Tick();}
 }catch(const std::exception&){server.Stop();zealServer.Stop();started=zealStarted=false;}
}
void ShutdownPipes(){server.Stop();zealServer.Stop();started=zealStarted=false;character.clear();lastOutput=0;}
void PipeChat(const char* message,int color){
 if(!options.enabled || !(server.Clients()+zealServer.Clients()) || !message)return;
 try{std::string clean;for(std::size_t i=0;i<4096 && message[i];++i)if(static_cast<unsigned char>(message[i])>=32)clean+=message[i];
  Publish(PipeType::Log,nlohmann::json{{"type",color},{"text",Utf8(clean)}}.dump());
 }catch(const std::exception&){}
}
std::string PipeStatus(){return "Pipe: \\\\.\\pipe\\gusto_"+std::to_string(GetCurrentProcessId())+", "+(options.enabled?"on":"off")+", clients="+std::to_string(server.Clients())+", zeal="+(options.zealAlias?"on":"off")+", zeal clients="+std::to_string(zealServer.Clients())+", delay="+std::to_string(options.delay)+"ms, drops="+std::to_string(server.Drops())+", error="+std::to_string(server.Error());}
bool HandlePipeCommand(std::string_view text,std::string& reply){
 std::istringstream input{std::string(text)};std::string cmd,value;input>>cmd;
 if(cmd=="/gusto"){input>>value;if(value!="pipes" && value!="pipe")return false;input>>value;
  if(value.empty()||value=="status"){reply=PipeStatus();return true;}
  auto cfg=options;if(value=="zeal"){std::string state;input>>state;if(state=="on")cfg.zealAlias=true;else if(state=="off")cfg.zealAlias=false;else{reply="Use /gusto pipes zeal [on|off].";return true;}}else if(value=="on")cfg.enabled=true;else if(value=="off")cfg.enabled=false;else{reply="Use /gusto pipes [on|off|status].";return true;}SetPipeOptions(cfg);reply=PipeStatus();return true;
 }
 if(cmd!="/pipe" && cmd!="/pipedelay" && cmd!="/pipeverbose")return false;
 if(cmd=="/pipe"){
  std::string data;std::getline(input>>std::ws,data);if(data.size()>4096){reply="Pipe text exceeds 4096 bytes.";return true;}
  if(!options.enabled){reply="Gusto pipes are off.";return true;}
  try{Publish(PipeType::Custom,nlohmann::json{{"text",Utf8(data)}}.dump());}catch(const std::exception&){reply="Unable to encode pipe text.";}return true;
 }
 auto cfg=options;input>>value;
 if(cmd=="/pipedelay"){
  int delay=-1;std::istringstream number(value);char extra;
  if(!(number>>delay)||(number>>extra)||delay<0||delay>5000){reply="Use /pipedelay <0 or 25-5000 milliseconds>.";return true;}cfg.delay=delay;
 }else{if(value.empty())cfg.verbose=!cfg.verbose;else if(value=="on")cfg.verbose=true;else if(value=="off")cfg.verbose=false;else{reply="Use /pipeverbose [on|off].";return true;}}
 SetPipeOptions(cfg);reply=PipeStatus()+", verbose="+(cfg.verbose?"on":"off");return true;
}
}
