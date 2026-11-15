// PicoCalc-side helper for the DOS JSON save tests (LPS-DOS-115-CHG04).
// Not part of the DOS build: it compiles the PicoCalc v1.15 application code
// (LPS-Companion-PicoCalc, src/lps_companion.cpp and tests/save_helpers.h) so
// the fixtures are written by the real PicoCalc writer, and so a DOS-written
// save can be checked with the real PicoCalc reader.
//
//   g++ -std=c++17 -I<PicoCalc repo>/src -I<PicoCalc repo>/tests \
//       tools/picocalc_fixture.cpp -o picocalc_fixture
//   ./picocalc_fixture full tests/fixtures/picocalc_full.jsn
//   ./picocalc_fixture custom tests/fixtures/picocalc_custom.jsn
//   ./picocalc_fixture resave SAVE_B.JSN OUT.JSN   (read with readSave, write back)
//   ./picocalc_fixture check A.JSN B.JSN ...       (one verdict per file)
#include "lps_companion.cpp"
#include "save_helpers.h"
#include <cstdio>
#include <string>
using namespace lps;

static bool writeFile(const char* path,const State& s,uint32_t generation){
    std::array<char,lps_test::MemoryCapacity> bytes{};std::size_t n=0;
    lps_test::MemorySink sink(bytes,n);uint32_t checksum=0;
    if(!writeSave(s,generation,sink,checksum))return false;
    FILE* f=std::fopen(path,"wb");if(!f)return false;
    const bool ok=std::fwrite(bytes.data(),1,n,f)==n;return std::fclose(f)==0&&ok;
}
static void tick(State& s,Day& d,const char* name){
    const unsigned i=indexByStoredName(s,name);
    if(i<s.activityCount)d.flags|=ActivityFlags(1u)<<i;
    else copyActivityName(d.names[d.namedCount++],name); // frozen: not in the catalog
}
static void text(char* out,std::size_t n,const char* value){std::snprintf(out,n,"%s",value);}
// A PicoCalc user's save: the catalog reordered and extended (a custom entry,
// an English-only one), French text with accents, frozen names, the Meal
// Planner deck and hidden home entries.
static State custom(){
    State s;s.language=Language::French;s.palette=Palette::Blue;s.xp=4321;
    swapActivities(s,0,16); // "Sport" first, "Marche" in its place
    Activity& velo=s.activities[s.activityCount++];velo.id=s.nextActivityId++;copyActivityName(velo.french,"Vélo");copyActivityName(velo.english,"Cycling");
    Activity& chess=s.activities[s.activityCount++];chess.id=s.nextActivityId++;copyActivityName(chess.english,"Chess");
    copyActivityName(s.activities[5].french,"Projet perso"); // renamed "Projet"
    auto day=[&](Day& d,Date date,uint32_t number){d=Day{};d.date=date;d.number=number;};
    for(unsigned i=0;i<5;++i){
        Day& d=s.history[i];day(d,{2026,11,uint8_t(9+i)},35+i);
        tick(s,d,"Marche");tick(s,d,i%2?"Sport":"Bible");
        if(i==1){tick(s,d,"Vélo");tick(s,d,"Tai-chi");d.expected=83000;d.actual=82600;text(d.note.data(),d.note.size(),"Séance à la piscine, très bien");}
        if(i==2){tick(s,d,"Projet");tick(s,d,"Chess");text(d.note.data(),d.note.size(),"Nom gelé \"Projet\"");}
        if(i==3){tick(s,d,"Méditation");tick(s,d,"Congé");}
    }
    s.count=5;s.next=5;
    day(s.today,{2026,11,14},40);
    tick(s,s.today,"Messe");tick(s,s.today,"Sport");tick(s,s.today,"Vélo");tick(s,s.today,"Chess");tick(s,s.today,"Projet perso");
    s.today.expected=82500;text(s.today.note.data(),s.today.note.size(),"Noël en famille, gâteau à l'orange");
    s.taskCount=3;
    s.tasks[0].status=TaskStatus::Doing;text(s.tasks[0].name.data(),s.tasks[0].name.size(),"Réparer le vélo");
    s.tasks[1].status=TaskStatus::Todo;text(s.tasks[1].name.data(),s.tasks[1].name.size(),"Écrire à Hélène");
    s.tasks[2].status=TaskStatus::Done;text(s.tasks[2].name.data(),s.tasks[2].name.size(),"Plan \\ \"B\"");
    s.reminderCount=2;
    s.reminders[0].from={2026,11,10};s.reminders[0].duration=30;text(s.reminders[0].text.data(),s.reminders[0].text.size(),"Anniversaire de François");
    s.reminders[1].from={2026,12,1};s.reminders[1].duration=1;text(s.reminders[1].text.data(),s.reminders[1].text.size(),"Rendez-vous médecin");
    toggleMealCard(s.meals,3);toggleMealCard(s.meals,0);toggleMealCard(s.meals,15);
    s.hiddenHome=uint16_t((1u<<3)|(1u<<4)); // kanban, rosary
    return s;
}
int main(int argc,char** argv){
    const std::string mode=argc>1?argv[1]:"";
    if(mode=="full"&&argc==3){
        State s=lps_test::fullState('y');
        toggleMealCard(s.meals,7);toggleMealCard(s.meals,2);s.hiddenHome=uint16_t(1u<<6);
        return writeFile(argv[2],s,41)?0:1;
    }
    if(mode=="custom"&&argc==3)return writeFile(argv[2],custom(),7)?0:1;
    if(mode=="resave"&&argc==4){
        FILE* f=std::fopen(argv[2],"rb");if(!f)return 2;
        static std::array<char,lps_test::MemoryCapacity> bytes{};const std::size_t n=std::fread(bytes.data(),1,bytes.size(),f);std::fclose(f);
        lps_test::MemorySource source(bytes.data(),n);State s;SaveInfo info;
        const SaveCheck r=readSave(source,s,info);
        if(r!=SaveCheck::Valid){std::fprintf(stderr,"PicoCalc readSave: not valid (%d)\n",int(r));return 3;}
        return writeFile(argv[3],s,info.generation)?0:1;
    }
    if(mode=="check"){
        static const char* const names[]={"valid","damaged","unsupported","ioerror"};
        for(int i=2;i<argc;++i){
            FILE* f=std::fopen(argv[i],"rb");if(!f){std::printf("missing\n");continue;}
            static std::array<char,lps_test::MemoryCapacity+1> bytes{};const std::size_t n=std::fread(bytes.data(),1,bytes.size(),f);std::fclose(f);
            lps_test::MemorySource source(bytes.data(),n,256);State s;SaveInfo info;
            std::printf("%s\n",names[int(readSave(source,s,info))]);
        }
        return 0;
    }
    std::fprintf(stderr,"usage: %s full|custom OUT.JSN | resave IN.JSN OUT.JSN | check FILE...\n",argv[0]);
    return 2;
}
