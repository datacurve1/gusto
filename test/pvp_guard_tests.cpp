#include "gusto/client/rof2/pvp_guard_model.hpp"
#include <cstdio>
#include <cstdlib>
using namespace gusto::client::rof2;
int main() {
  int count=0; auto check=[&](bool ok){++count;if(!ok){std::printf("FAIL PvP guard check %d\n",count);std::exit(1);}};
  check(IsPvpZoneName("Velketor's Labyrinth (PVP)"));
  check(IsPvpZoneName("Zone [ PvP ]")); check(!IsPvpZoneName("Zone (GUILD)")); check(!IsPvpZoneName("PvP discussion"));
  for(auto command:{"#pvp"," /say #PVP ","/s #pvp off","/pvp off","#set pvp off","#setpvp 0","#pvp false","#pvp disable","#pvp toggle"}) {
    check(ShouldBlockPvpOff(command,true,true)); check(!ShouldBlockPvpOff(command,false,true));
  }
  for(auto command:{"#pvp on","#pvp 1","/pvp enable","#set pvp true","/say hello","#pvp_points 0","/pvpleaderboard","#set level 10"}) check(!ShouldBlockPvpOff(command,true,true));
  check(!ShouldBlockPvpOff("#pvp",true,false)); check(ShouldBlockPvpOff("#pvp off",true,false));
  std::printf("PASS: %d PvP zone markers, toggle/off blocks, enabling and ordinary-command checks\n",count);
}
