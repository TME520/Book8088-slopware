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
//   ./picocalc_fixture intl tests/fixtures/picocalc_intl.jsn   (LPS-DOS-115-CHG06d)
//   ./picocalc_fixture close IN.JSN OUT.ICS OUT.CSV OUT.JSN
//       closes today as the PicoCalc's Finish screen does: the weight CSV row
//       (into an empty year file), the ICS (main.cpp exportIcs, written to a
//       file instead of FatFs) and the save committed afterwards.
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
// LPS-DOS-115-CHG06d: text that CP437 lacks (œ, À, €, an emoji) in every
// text a save holds: catalog names, frozen day names, notes, tasks, reminders.
static State intl(){
    State s;s.language=Language::French;s.palette=Palette::Green;s.xp=777;
    copyActivityName(s.activities[0].french,"Marche € 🙂");      // 14 bytes
    copyActivityName(s.activities[1].english,"Œuvre À faire");
    Activity& oeuf=s.activities[s.activityCount++];oeuf.id=s.nextActivityId++;copyActivityName(oeuf.french,"Œufs; cœur, À 🍳");copyActivityName(oeuf.english,"Eggs 🍳");
    auto day=[&](Day& d,Date date,uint32_t number){d=Day{};d.date=date;d.number=number;};
    for(unsigned i=0;i<3;++i){
        Day& d=s.history[i];day(d,{2026,11,uint8_t(12+i)},20+i);
        tick(s,d,"Marche € 🙂");tick(s,d,"Œufs; cœur, À 🍳");
        if(i==1){tick(s,d,"Pâtes 🍝 À l'œil");d.expected=80500;text(d.note.data(),d.note.size(),"Cœur: À 5 € 🙂, \"ok\" \\ fin; oui");}
    }
    s.count=3;s.next=3;
    day(s.today,{2026,11,15},23);
    tick(s,s.today,"Marche € 🙂");tick(s,s.today,"Œufs; cœur, À 🍳");tick(s,s.today,"Messe");
    s.today.expected=80250;s.today.actual=80125;
    text(s.today.note.data(),s.today.note.size(),"Œuvre du jour, À 10 € 😀; « bien »\\N fin");
    s.taskCount=3;
    s.tasks[0].status=TaskStatus::Todo;text(s.tasks[0].name.data(),s.tasks[0].name.size(),"Acheter œufs À 3 € 🙂");
    s.tasks[1].status=TaskStatus::Doing;text(s.tasks[1].name.data(),s.tasks[1].name.size(),"Œuvre, 😀 \"x\" \\ y");
    s.tasks[2].status=TaskStatus::Done;text(s.tasks[2].name.data(),s.tasks[2].name.size(),"À l'œil: 2 €");
    s.reminderCount=2;
    s.reminders[0].from={2026,11,15};s.reminders[0].duration=10;text(s.reminders[0].text.data(),s.reminders[0].text.size(),"Fête 🎉 À 20h, 15 €");
    s.reminders[1].from={2026,11,1};s.reminders[1].duration=60;text(s.reminders[1].text.data(),s.reminders[1].text.size(),"Cœur œuvre ŒÀ€");
    return s;
}
static bool readFile(const char* path,State& s,uint32_t& generation){
    FILE* f=std::fopen(path,"rb");if(!f)return false;
    static std::array<char,lps_test::MemoryCapacity> bytes{};const std::size_t n=std::fread(bytes.data(),1,bytes.size(),f);std::fclose(f);
    lps_test::MemorySource source(bytes.data(),n);SaveInfo info;
    if(readSave(source,s,info)!=SaveCheck::Valid)return false;
    generation=info.generation;return true;
}
// main.cpp PicoCalc::put / escaped / exportIcs / exportWeightCsv, FILE* for FIL.
static bool put(FILE* f,const char* text){const auto n=std::strlen(text);return std::fwrite(text,1,n,f)==n;}
static bool escaped(FILE* f,const char* text){
    char b[3]{};
    for(;*text;++text){char c=*text;
        if(c=='\\'||c==';'||c==','){b[0]='\\';b[1]=c;b[2]=0;}
        else if(c=='\n'){std::strcpy(b,"\\n");}
        else {b[0]=c;b[1]=0;}
        if(!put(f,b))return false;
    }
    return true;
}
static bool exportIcs(const char* path,const Day& day,Language language,const State& state){
    if(!validDate(day.date))return false;
    char ymd[13],next[13],line[160];
    std::snprintf(ymd,sizeof ymd,"%04u%02u%02u",day.date.year,day.date.month,day.date.day);
    auto tomorrow=day.date;if(!nextDate(tomorrow))return false;
    std::snprintf(next,sizeof next,"%04u%02u%02u",tomorrow.year,tomorrow.month,tomorrow.day);
    FILE* f=std::fopen(path,"wb");if(!f)return false;
    bool ok=put(f,"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//LPS Companion//EN\r\nCALSCALE:GREGORIAN\r\nBEGIN:VEVENT\r\n");
    std::snprintf(line,sizeof line,"UID:lps-companion-%s@picocalc\r\nDTSTART;VALUE=DATE:%s\r\nDTEND;VALUE=DATE:%s\r\n",ymd,ymd,next);ok=ok&&put(f,line);
    ok=ok&&put(f,"SUMMARY:LPS Companion\r\nDESCRIPTION:");
    const char* names=language==Language::English?"Activities: ":"Activités: ";ok=ok&&escaped(f,names);
    bool first=true;eachExportedActivity(state,day,language,[&](const char* name){
        if(!first)ok=ok&&put(f,", ");
        ok=ok&&escaped(f,name);first=false;});
    if(first)ok=ok&&put(f,"none");
    ok=ok&&put(f,"\\NXP:");
    if(day.expected>=0){ok=ok&&put(f,"\\N");std::snprintf(line,sizeof line,"%s: %ld.%03ld kg",language==Language::English?"Expected weight":"Poids attendu",long(day.expected/1000),long(day.expected%1000));ok=ok&&escaped(f,line);}
    if(day.actual>=0){ok=ok&&put(f,"\\N");std::snprintf(line,sizeof line,"%s: %ld.%03ld kg",language==Language::English?"Actual weight":"Poids effectif",long(day.actual/1000),long(day.actual%1000));ok=ok&&escaped(f,line);}
    if(day.note[0]){ok=ok&&put(f,"\\NNote: ");ok=ok&&escaped(f,day.note.data());}
    ok=ok&&put(f,"\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    return std::fclose(f)==0&&ok;
}
static bool exportWeightCsv(const char* path,const Day& day){ // a new year file
    char date[16],expected[16]{},actual[16]{},line[64];
    std::snprintf(date,sizeof date,"%04u-%02u-%02u",day.date.year,day.date.month,day.date.day);
    if(day.expected>=0)std::snprintf(expected,sizeof expected,"%ld.%03ld",long(day.expected/1000),long(day.expected%1000));
    if(day.actual>=0)std::snprintf(actual,sizeof actual,"%ld.%03ld",long(day.actual/1000),long(day.actual%1000));
    std::snprintf(line,sizeof line,"%s,%s,%s\r\n",date,expected,actual);
    FILE* f=std::fopen(path,"wb");if(!f)return false;
    const bool ok=put(f,"date,expected_kg,measured_kg\r\n")&&put(f,line);
    return std::fclose(f)==0&&ok;
}
// lps_companion.cpp App::key, Screen::Finish + Enter (without the UI).
static int closeDay(const char* in,const char* ics,const char* csv,const char* out){
    State state;uint32_t generation=0;if(!readFile(in,state,generation))return 3;
    const uint32_t gain=points(state.today);
    State next=state;const unsigned slot=next.next;next.history[slot]=next.today;freezeDayNames(next,next.history[slot]);
    next.next=uint8_t((next.next+1)%HistoryLength);if(next.count<HistoryLength)++next.count;
    Date tomorrow=state.today.date;if(!nextDate(tomorrow))return 4;
    if(!exportWeightCsv(csv,state.today))return 5;
    if(!exportIcs(ics,next.history[slot],state.language,next))return 6;
    next.xp+=gain;next.today=Day{};next.today.number=state.today.number+1;next.today.date=tomorrow;
    next.meals=MealDeckState{};
    return writeFile(out,next,generation+1)?0:1;
}
int main(int argc,char** argv){
    const std::string mode=argc>1?argv[1]:"";
    if(mode=="full"&&argc==3){
        State s=lps_test::fullState('y');
        toggleMealCard(s.meals,7);toggleMealCard(s.meals,2);s.hiddenHome=uint16_t(1u<<6);
        return writeFile(argv[2],s,41)?0:1;
    }
    if(mode=="custom"&&argc==3)return writeFile(argv[2],custom(),7)?0:1;
    if(mode=="intl"&&argc==3)return writeFile(argv[2],intl(),12)?0:1;
    if(mode=="close"&&argc==6)return closeDay(argv[2],argv[3],argv[4],argv[5]);
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
    std::fprintf(stderr,"usage: %s full|custom|intl OUT.JSN | resave IN.JSN OUT.JSN | check FILE... | close IN.JSN OUT.ICS OUT.CSV OUT.JSN\n",argv[0]);
    return 2;
}
