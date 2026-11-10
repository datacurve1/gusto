#include "gusto/client/rof2/pipe_model.hpp"
#include "gusto/third_party/json.hpp"
#include <cstdio>
#include <cstdlib>
using namespace gusto::client::rof2;
using Json=nlohmann::json;
int main(){
 int count=0;auto check=[&](bool b){if(!b){std::printf("FAIL: pipe model check %d\n",count+1);std::exit(1);}++count;};
 PipeSnapshot s;s.self={"Aggatha",123,1,2,3,64,50,100,60,11,1,0,true};s.zone=202;s.mana=250;s.maxMana=500;s.endurance=800;s.maxEndurance=1000;
 s.target=PipeEntity{"a_skeleton",456,3,4,5,32,80,100,45,1,60,1,true};s.pet=789;
 s.group.push_back({"Friend",987,10,20,30,256,75,100,60,2,1,0,true});s.raid.push_back({s.group[0],1,"Group Leader"});
 auto messages=BuildPipeMessages(s,false);check(messages.size()==5);
 auto find=[&](int type){for(const auto& [t,data]:messages)if(t==type)return Json::parse(data);return Json();};
 auto player=find(3);check(player["zone"]==202 && player["spawn_id"]==123);check(player["target_id"]==456 && player["target_loc"]["x"]==3);check(player["pet_id"]==789);
 auto group=find(6);check(group[0]["name"]=="Friend" && !group[0].contains("hp_current"));
 auto raid=find(5);check(raid[0]["group"]=="2" && raid[0]["rank"]=="Group Leader");
 auto labels=find(1);bool name=false,mana=false;for(auto l:labels){if(l["type"]==1)name=l["value"]=="Aggatha";if(l["type"]==124)mana=l["value"]=="250";}check(name&&mana);
 auto gauges=find(2);auto value=[&](int type){for(auto& g:gauges)if(g["type"]==type)return g["value"].get<int>();return -1;};check(value(1)==500 && value(2)==500 && value(3)==800);
 s.target->x=1000;messages=BuildPipeMessages(s,true);player=find(3);check(!player.contains("target_loc"));group=find(6);check(group[0]["hp_current"]==75);
 s.target.reset();s.pet=0;s.group.clear();s.raid.clear();messages=BuildPipeMessages(s,false);player=find(3);check(!player.contains("target_id") && !player.contains("pet_id"));check(find(5).empty() && find(6).empty());
 s.self.hp=-1;s.self.maxHP=0;s.mana=-1;s.maxMana=0;s.endurance=-1;messages=BuildPipeMessages(s,false);check(find(2).empty());
 s.self.name="Quoted \"Name\"\n";messages=BuildPipeMessages(s,false);labels=find(1);check(labels[0]["value"]==s.self.name);
 std::printf("PASS: %d pipe snapshot checks (Zeal types, fields, resources, range, empty rosters, unknowns and escaping)\n",count);
}
