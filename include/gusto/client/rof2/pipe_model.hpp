#pragma once
#include <optional>
#include <string>
#include <vector>
#include <utility>
namespace gusto::client::rof2 {
struct PipeEntity {
 std::string name;unsigned id=0;float x=0,y=0,z=0,heading=0;
 int hp=-1,maxHP=-1,level=-1,cls=-1,race=-1,type=-1;bool position=false;
};
struct PipeRaidMember {PipeEntity entity;int group=-1;std::string rank;};
struct PipeSnapshot {
 PipeEntity self;unsigned zone=0,pet=0;bool autoattack=false;
 int mana=-1,maxMana=-1,endurance=-1,maxEndurance=-1;
 std::optional<PipeEntity> target,petEntity;
 std::vector<PipeEntity> group;std::vector<PipeRaidMember> raid;
};
// First is the numeric Zeal pipe_data_type; second is the inner JSON string.
std::vector<std::pair<int,std::string>> BuildPipeMessages(const PipeSnapshot&,bool verbose);
}
