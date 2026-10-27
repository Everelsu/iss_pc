// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <string_view>
namespace IssTrophies {
struct TrophyText {std::string_view name,requirement;};
// Zero-based game IDs; concise requirement paraphrases, not copied guide prose.
// Sources and ordering checks: work/trophies/TROPHY_TEXT_SOURCES.md.
inline constexpr std::array<TrophyText,48> Catalogue{{
 {"Enjoy Your Powers","Earn every other trophy."},
 {"Unstoppable","Complete the campaign on Expert."},
 {"Sacrifice","Choose to protect others despite personal risk."},
 {"Ruthless","Put your own safety first."},
 {"Temperance","Choose restraint."},
 {"Malice","Choose aggression over restraint."},
 {"Courage","Choose to defend vulnerable people."},
 {"Contempt","Choose to prey on the vulnerable."},
 {"Mercy","Choose forgiveness over revenge."},
 {"Revenge","Choose retaliation over forgiveness."},
 {"Justice","Choose to reveal the powerful's wrongdoing."},
 {"Dominance","Choose domination over justice."},
 {"Reconciliation","Complete the Good-karma story ending."},
 {"Rebellion","Complete the Evil-karma story ending."},
 {"Everybody Out","Free ten suspicion-pen prisoners."},
 {"Spin-off","Beat up ten sign twirlers."},
 {"Everyone's a Critic","Defeat ten street musicians."},
 {"Just Say No","Stop ten drug dealers."},
 {"Freedom of Speech","Break up ten activist gatherings."},
 {"What Did You Call Me?","Kill ten taunting Akuran gangsters."},
 {"Someone Your Own Size","Save ten escaped conduits from mobs."},
 {"Walk It Off","Treat ten injured civilians."},
 {"Kicking and Screaming","Execute ten injured enemies or civilians."},
 {"True Hero","Reach Good karma rank five."},
 {"Infamous","Reach Evil karma rank five."},
 {"Bweep! Bweep! Bweep!","Collect 50% of Blast Shards."},
 {"Shardcore","Collect every Blast Shard."},
 {"Nothing To See Here","Break 50% of D.U.P. security cameras."},
 {"Blinded","Break every D.U.P. security camera."},
 {"Inside Track","Find every informant recording."},
 {"Blown Cover","Eliminate every D.U.P. secret agent."},
 {"Big Brother","Break every D.U.P. hidden camera."},
 {"Coloring Inside the Lines","Finish every stencil artwork."},
 {"Prank Call","Win one District Showdown."},
 {"Hello Operator","Win showdowns across half the districts."},
 {"Final Showdown","Win every District Showdown."},
 {"One Down","Completely liberate one district."},
 {"Home Field Advantage","Completely liberate half the districts."},
 {"Clean Sweep","Completely liberate Seattle."},
 {"Total CARnage","Wreck five vehicles within five seconds."},
 {"Drop Everything","Perform a Comet Drop exceeding 170 metres."},
 {"Keep It Up","Remain airborne beyond 45 seconds."},
 {"Put That Anywhere","Kill ten enemies with exploding ammunition crates."},
 {"Hat Trick","Defeat one enemy using three power types."},
 {"Double Smoke Stack Attack","Smoke Dash through two red vents without landing; Comet Drop an enemy."},
 {"Cough Drop","Comet Drop onto three coughing enemies."},
 {"Make Fetch Proud","Strike three weak points using Neon Beam within six seconds."},
 {"Excessive Force","Kill exactly one enemy with a Karmic Streak."}
}};
constexpr const TrophyText* TextForTrophy(int id){return id>=0&&id<int(Catalogue.size())?&Catalogue[size_t(id)]:nullptr;}
}
