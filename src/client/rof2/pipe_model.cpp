// Adapted from Zeal named_pipe.cpp. IDs and envelope data remain compatible.
#include "gusto/client/rof2/pipe_model.hpp"
#include "gusto/third_party/json.hpp"
#include <algorithm>
#include <cmath>
namespace gusto::client::rof2 {
using Json=nlohmann::json;
namespace {
Json Loc(const PipeEntity& e){return Json{{"x",e.x},{"y",e.y},{"z",e.z}};}
int Percent(int current,int max,int scale=100){return max>0&&current>=0?int(std::clamp(double(current)/max,0.,1.)*scale):-1;}
void Field(Json& j,const char* key,int value){if(value>=0)j[key]=value;}
Json Member(const PipeEntity& e,bool verbose,unsigned zone){
 Json j={{"name",e.name}};if(e.id)j["spawn_id"]=e.id;
 if(e.position){j["loc"]=Loc(e);j["heading"]=e.heading;}
 if(verbose){Field(j,"hp_current",e.hp);Field(j,"hp_max",e.maxHP);Field(j,"class",e.cls);Field(j,"level",e.level);if(e.id)j["zone_id"]=zone;}
 return j;
}
}
std::vector<std::pair<int,std::string>> BuildPipeMessages(const PipeSnapshot& s,bool verbose){
 std::vector<std::pair<int,std::string>> output;
 auto push=[&](int type,const Json& j){output.emplace_back(type,j.dump(-1,' ',false,Json::error_handler_t::replace));};
 Json raid=Json::array(),group=Json::array();
 for(const auto& m:s.raid){auto j=Member(m.entity,verbose,s.zone);j["group"]=std::to_string(m.group>=0&&m.group<12?m.group+1:0);Field(j,"level",m.entity.level);Field(j,"class",m.entity.cls);j["rank"]=m.rank;raid.push_back(j);}
 for(const auto& e:s.group)group.push_back(Member(e,verbose,s.zone));
 // Empty arrays are deliberate: consumers can clear a departed group/raid.
 push(5,raid);push(6,group);
 Json labels=Json::array(),gauges=Json::array();
 auto label=[&](int type,std::string value){labels.push_back(Json{{"type",type},{"value",value},{"meta",Json::object()}});};
 auto number=[&](int type,int value){if(value>=0)label(type,std::to_string(value));};
 auto gauge=[&](int type,int value){if(value>=0)gauges.push_back(Json{{"type",type},{"text",std::to_string(value/10)+"%"},{"value",value}});};
 const char* classes[]={"Unknown","Warrior","Cleric","Paladin","Ranger","Shadow Knight","Druid","Monk","Bard","Rogue","Shaman","Necromancer","Wizard","Magician","Enchanter","Beastlord","Berserker"};
 label(1,s.self.name);number(2,s.self.level);if(s.self.cls>=0 && s.self.cls<=16)label(3,classes[s.self.cls]);
 number(17,s.self.hp);number(18,s.self.maxHP);number(19,Percent(s.self.hp,s.self.maxHP));
 number(20,Percent(s.mana,s.maxMana));number(21,Percent(s.endurance,s.maxEndurance));
 label(28,s.target?s.target->name:"");if(s.target)number(29,Percent(s.target->hp,s.target->maxHP));
 for(std::size_t i=0;i<s.group.size() && i<5;++i){label(30+int(i),s.group[i].name);number(35+int(i),Percent(s.group[i].hp,s.group[i].maxHP));gauge(11+int(i),Percent(s.group[i].hp,s.group[i].maxHP,1000));}
 label(68,s.petEntity?s.petEntity->name:"");if(s.petEntity)number(69,Percent(s.petEntity->hp,s.petEntity->maxHP));
 if(s.self.hp>=0 && s.self.maxHP>0)label(70,std::to_string(s.self.hp)+"/"+std::to_string(s.self.maxHP));
 if(s.mana>=0 && s.maxMana>0)label(80,std::to_string(s.mana)+"/"+std::to_string(s.maxMana));
 number(124,s.mana);number(125,s.maxMana);
 gauge(1,Percent(s.self.hp,s.self.maxHP,1000));gauge(2,Percent(s.mana,s.maxMana,1000));gauge(3,Percent(s.endurance,s.maxEndurance,1000));
 if(s.target)gauge(6,Percent(s.target->hp,s.target->maxHP,1000));
 if(s.petEntity)gauge(16,Percent(s.petEntity->hp,s.petEntity->maxHP,1000));
 push(1,labels);push(2,gauges);
 Json player={{"zone",s.zone},{"spawn_id",s.self.id},{"autoattack",s.autoattack}};
 if(s.self.position){player["location"]=Loc(s.self);player["heading"]=s.self.heading;}
 if(s.pet)player["pet_id"]=s.pet;
 if(s.target){const auto& t=*s.target;player["target_id"]=t.id;player["target_name"]=t.name;Field(player,"target_type",t.type);Field(player,"target_level",t.level);Field(player,"target_class",t.cls);Field(player,"target_race",t.race);
  double dx=t.x-s.self.x,dy=t.y-s.self.y,dz=t.z-s.self.z;
  if(s.self.position && t.position && dx*dx+dy*dy+dz*dz<250.*250.)player["target_loc"]=Loc(t);
 }
 push(3,player);return output;
}
}
