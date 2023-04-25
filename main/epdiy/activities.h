#include <vector>
using namespace std;

// Structure for an activity
struct Day_activity {
   uint8_t day_week; // 1 Monday to 5 Friday (Weekend sleeps)
   uint8_t hr_start;
   uint8_t mm_start;
   uint8_t hr_end;
   uint8_t mm_end;
   char * note;
};
vector<Day_activity> date_vector;

// Make a list of activities from Schedule table
Day_activity da;

void vector_add(const Day_activity & data) {
    date_vector.push_back(data);
}
// Activities data structure in Vector style
void activity_load() {
    // Monday
    da.day_week = 1;
    da.hr_start = 11;
    da.mm_start = 0;
    da.hr_end = 12;
    da.mm_end = 0;
    da.note = (char*)"STRIKING\nGRAPPLING\nOPEN MAT";
    vector_add(da); // 0
    da.hr_start = 14;
    da.mm_start = 0;
    da.hr_end = 15;
    da.mm_end = 0;
    vector_add(da); // 1

    da.hr_start = 17;
    da.mm_start = 0;
    da.hr_end = 17;
    da.mm_end = 30;
    da.note = (char*)"OPEN MAT";
    vector_add(da); // 2
    da.hr_start = 17;
    da.mm_start = 30;
    da.hr_end = 18;
    da.mm_end = 30;
    da.note = (char*)"BJJ KIDS\nCROSSFIGHT";
    vector_add(da); // 3
    da.hr_start = 18;
    da.mm_start = 30;
    da.hr_end = 19;
    da.mm_end = 30;
    da.note = (char*)"STRIKING\nGRAPPLING\nCROSSFIGHT";
    vector_add(da); // 4
    da.hr_start = 19;
    da.mm_start = 30;
    da.hr_end = 20;
    da.mm_end = 30;
    da.note = (char*)"STRIKING\nMMA\nCROSSFIGHT";
    vector_add(da); // 5
    // Tuesday
    da.day_week = 2;
    da.hr_start = 10; // 10
    da.mm_start = 0;
    da.hr_end = 11;
    da.mm_end = 0;
    da.note = (char*)"STRIKING\nGRAPPLING\nOPEN MAT";
    vector_add(da);
    da.hr_start = 11;
    da.mm_start = 0;
    da.hr_end = 12;
    da.mm_end = 0;
    da.note = (char*)"WRESTLING\nOPEN MAT\nCROSSFIGHT";
    vector_add(da);
    da.hr_start = 14;
    da.mm_start = 0;
    da.hr_end = 15;
    da.mm_end = 0;
    da.note = (char*)"STRIKING\nGRAPPLING\nOPEN MAT";
    vector_add(da);

    da.hr_start = 16; // 17
    da.mm_start = 0;
    da.hr_end = 17;
    da.mm_end = 30;
    da.note = (char*)"OPEN MAT";
    vector_add(da);
    da.hr_start = 17;
    da.mm_start = 30;
    da.hr_end = 18;
    da.mm_end = 30;
    da.note = (char*)"STRIKING TEENS\nGRAPP TEENS\nOPEN MAT";
    vector_add(da);
    da.hr_start = 18;
    da.mm_start = 30;
    da.hr_end = 19;
    da.mm_end = 30;
    da.note = (char*)"STRIKING\nWRESTLING\nOPEN MAT";
    vector_add(da);
    da.hr_start = 19;
    da.mm_start = 30;
    da.hr_end = 20;
    da.mm_end = 30;
    da.note = (char*)"STRIKING\nGRAPPLING\nOPEN MAT";
    vector_add(da);
    // Wed
    da.day_week = 3;
    da.hr_start = 11;
    da.mm_start = 0;
    da.hr_end = 12;
    da.mm_end = 0;
    da.note = (char*)"STRIKING\nGRAPPLING\nOPEN MAT";
    vector_add(da);
    da.hr_start = 14;
    da.mm_start = 0;
    da.hr_end = 15; //15
    da.mm_end = 0;
    vector_add(da);

    da.hr_start = 17;
    da.mm_start = 0;
    da.hr_end = 17;
    da.mm_end = 30;
    da.note = (char*)"OPEN MAT";
    vector_add(da);
    da.hr_start = 17;
    da.mm_start = 30;
    da.hr_end = 18;
    da.mm_end = 30;
    da.note = (char*)"STRIKING\nKIDS\n\nCROSSFIGHT"; // Comes out together like STRIKINGKIDS eating the space
    vector_add(da);
    da.hr_start = 18;
    da.mm_start = 30;
    da.hr_end = 19;
    da.mm_end = 30;
    da.note = (char*)"STRIKING\nGRAPPLING\nCROSSFIGHT";
    vector_add(da);
    da.hr_start = 19;
    da.mm_start = 30;
    da.hr_end = 20;
    da.mm_end = 30;
    da.note = (char*)"STRIKING\nMMA\nCROSSFIGHT";
    vector_add(da);
    // Thursday
    da.day_week = 4;
    da.hr_start = 10;
    da.mm_start = 0;
    da.hr_end = 11;
    da.mm_end = 0;
    da.note = (char*)"STRIKING\nBJJ\nOPEN MAT";
    da.hr_start = 11;
    da.mm_start = 0;
    da.hr_end = 12;
    da.mm_end = 0;
    da.note = (char*)"WRESTLING\nOPEN MAT\nCROSSFIGHT";
    vector_add(da);
    da.hr_start = 14;
    da.mm_start = 0;
    da.hr_end = 15;
    da.mm_end = 0;
    da.note = (char*)"STRIKING\nBJJ\nOPEN MAT";
    vector_add(da);

    da.hr_start = 17;
    da.mm_start = 0;
    da.hr_end = 17;
    da.mm_end = 30;
    da.note = (char*)"OPEN MAT";
    vector_add(da);
    da.hr_start = 17;
    da.mm_start = 30;
    da.hr_end = 18;
    da.mm_end = 30;
    da.note = (char*)"STRIKING TEENS\nBJJ TEENS\nOPEN MAT";
    vector_add(da);
    da.hr_start = 18;
    da.mm_start = 30;
    da.hr_end = 19;
    da.mm_end = 30;
    da.note = (char*)"STRIKING\nWRESTLING\nOPEN MAT";
    vector_add(da);
    da.hr_start = 19;
    da.mm_start = 30;
    da.hr_end = 20;
    da.mm_end = 30;
    da.note = (char*)"STRIKING\nBJJ\nOPEN MAT";
    vector_add(da);
    // Friday
    da.day_week = 5;
    da.hr_start = 11;
    da.mm_start = 0;
    da.hr_end = 12;
    da.mm_end = 0;
    da.note = (char*)"STRIKING\nGRAPPLING\nOPEN MAT";
    vector_add(da);

    da.hr_start = 17;
    da.mm_start = 0;
    da.hr_end = 17;
    da.mm_end = 30;
    da.note = (char*)"OPEN MAT";
    vector_add(da);
    da.hr_start = 17;
    da.mm_start = 30;
    da.hr_end = 18;
    da.mm_end = 30;
    da.note = (char*)"STRIKING BASICS\nGRAP BASICS\nOPEN MAT";
    vector_add(da);
    da.hr_start = 18;
    da.mm_start = 30;
    da.hr_end = 19;
    da.mm_end = 30;
    da.note = (char*)"GRAP SPARRING\nMMA SPARRING\nCROSSFIGHT";
    vector_add(da);
    da.hr_start = 19;
    da.mm_start = 30;
    da.hr_end = 20;
    da.mm_end = 30;
    da.note = (char*)"STRIKING SPARRING\nOPEN MAT";
    vector_add(da);
}