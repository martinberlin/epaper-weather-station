// Chinese Mandarin translation: Sunday to Saturday
char weekday_t[][12] = {
    "[]_",     "[]a",   "[]b",   "[]c",   "[]d",   "[]e",   "[]f"
    //"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六" 
    };

// Days of week + MONTH (below)
/* 星 [ 期 ]  DAY all toghether: [] 
TTF glyph description
 a   一     1: monday OK
 b   二     2  OK
 c   三     3  OK
 d   四     4  OK
 e   五     5: friday OK
 f   六     6: saturday OK
 _   日     sunday OK
*/

// Days of month
char month_t[][12] = {
    // January to June
    "a^", "b^", "c^", "d^", "e^", "f^",
    //"一月", "二月", "三月", "四月", "五月", "六月", 
    // July to December
      "g^",  "h^",   "i^", "j^", "ja^", "jb^",
    //"七月", "八月", "九月", "十月", "十一月", "十二月"
    };

/* NOTE: a->f from 1 to 6 is the same as weekdays above
TTF glyph description
 g  七  7 July
 h  八  8 August 
 i  九  9 September OK
 j  十  10 October OK
 ja 十一 11 November OK
 jb 十二 12 Dec OK

TEMPERATURE
TTF glyph 
 ¿  温  temp. 
 Ö  度  temp.
 Ä  浓  concentration of
 Ü  湿  water (humidity)
*/

char temperature_string[] = 
"%.1f C ¿Ö";    // 温度
char co2_string[]         = 
"%d CO2ÄÖ";     // 浓度
char humidity_string[]    = 
"%.1f %% ÜÖ";   // 湿度
