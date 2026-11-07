/*
  GENKEND DET! – lysshow
  Arduino Uno + WS2812B LED-kæde (BTF-Lighting)

  Tilslutning:
    Kædens 5V  -> Arduino 5V
    Kædens GND -> Arduino GND
    Kædens DI  -> Arduino pin 6 (gerne via en modstand på ca. 330 ohm)
    Brug kædens DI-ende (pilene peger VÆK fra den ende).

  Kræver biblioteket "FastLED" (Arduino IDE: Værktøjer > Administrer biblioteker > søg FastLED).
*/
#include <FastLED.h>

// ---------- RET HER ----------
#define NUM_LEDS   140    // antal LED'er på kæden (tæl dem, typisk 30, 60 eller 144 pr. meter)
#define LED_PIN    6
#define MAX_MA     400   // strømgrænse i milliampere. 400 er sikkert via USB uden strømforsyning
#define LYSSTYRKE  180   // 0-255
// -----------------------------

CRGB leds[NUM_LEDS];

// grundtilstand (sat af spillet)
char mode = 'I';          // starter med regnbue, så du kan se at det virker
CRGB baseCol = CRGB(255, 0, 120);
int  val = 100;

// kort effekt oven i grundtilstanden
char fx = 'f';
unsigned long fxStart = 0;
unsigned int  fxLen = 1400;

// seriel modtagelse
char buf[72];
byte len = 0;
bool synced = false;       // true når computeren svarer, så vi kun opdaterer lyset mellem beskederne
bool waiting = false;      // har sendt 'k' og venter på svar
unsigned long askedAt = 0, lastFrame = 0;

CRGB hexCol(const char *s) {
  char h[7]; memcpy(h, s, 6); h[6] = 0;
  long v = strtol(h, NULL, 16);
  return CRGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
}

bool isHex6(const char *s) {
  for (byte i = 0; i < 6; i++) if (!isxdigit(s[i])) return false;
  return true;
}

void command(char *s) {
  char c = s[0];
  if (!c) return;
  if (c >= 'a' && c <= 'z') {                       // effekter
    unsigned int l = c == 'g' ? 1700 : c == 'r' ? 1000 : c == 'b' ? 1200 : c == 'f' ? 1400 : c == 't' ? 110 : 0;
    if (!l) return;
    fx = c; fxStart = millis(); fxLen = l;
    return;
  }
  if (!strchr("OIPBASWDMX", c)) return;              // ukendt -> ignorer
  if (c == 'B' || c == 'X') {
    if (strlen(s) < 7 || !isHex6(s + 1)) return;
    baseCol = hexCol(s + 1);
    val = strlen(s) > 7 ? atoi(s + 7) : 100;
  } else {
    val = atoi(s + 1);
  }
  mode = c;
  val = constrain(val, 0, 100);
}

void line(char *s) {                               // flere kommandoer adskilt af ;
  char *p = strtok(s, ";");
  while (p) { command(p); p = strtok(NULL, ";"); }
}

// ---------- grundtilstande ----------
void bar(int pct, CRGB on, CRGB off) {
  long lit = (long)NUM_LEDS * 256 * pct / 100;     // i 1/256 LED
  for (int i = 0; i < NUM_LEDS; i++) {
    long here = lit - (long)i * 256;
    if (here >= 256) leds[i] = on;
    else if (here > 0) { leds[i] = blend(off, on, (uint8_t)here); }
    else leds[i] = off;
  }
}

// scanner: et lys løber frem og tilbage, grøn -> rød og hurtigere når svartiden løber ud
void scanner(unsigned long now) {
  static unsigned long lastP = 0;
  static float ph = 0;                           // 0-1 = ud, 1-2 = tilbage
  byte hue = map(val, 0, 100, 0, 96);            // 0 = rød (tiden er gået), 96 = grøn (start)
  float dt = min(50UL, now - lastP) / 1000.0; lastP = now;
  float sweep = 0.3 + 0.5 * val / 100.0;         // sekunder pr. tur: 0,8 i starten, 0,3 til sidst
  ph += dt / sweep; while (ph >= 2) ph -= 2;
  float pos = (ph < 1 ? ph : 2 - ph) * (NUM_LEDS - 1);
  fadeToBlackBy(leds, NUM_LEDS, 40);             // halen: højere tal = kortere hale
  int i0 = (int)pos; float f = pos - i0;         // fordel lyset blødt på to LED'er
  leds[i0] += CHSV(hue, 255, (1 - f) * 255);
  if (i0 + 1 < NUM_LEDS) leds[i0 + 1] += CHSV(hue, 255, f * 255);
}

void drawBase(unsigned long now) {
  switch (mode) {
    case 'O':                                        // slukket
      fill_solid(leds, NUM_LEDS, CRGB::Black); break;
    case 'I':                                        // lobby: rolig regnbue
      fill_rainbow(leds, NUM_LEDS, now / 25, max(1, 255 / NUM_LEDS)); break;
    case 'P':                                        // mens man tænker: slukket
      fill_solid(leds, NUM_LEDS, CRGB::Black); break;
    case 'B':                                        // en spiller har buzzet og svarer: scanner
      scanner(now); break;
    case 'A': {                                      // svaret vises: varm guld der ånder
      CRGB c(255, 150, 40); c.nscale8_video(beatsin8(18, 70, 255));
      fill_solid(leds, NUM_LEDS, c); break; }
    case 'S':                                        // stillingen: regnbue-løbelys
      for (int i = 0; i < NUM_LEDS; i++)
        leds[i] = ((i + now / 90) % 3 == 0) ? CRGB(CHSV(i * 5 + now / 12, 255, 255)) : CRGB(0, 0, 0);
      break;
    case 'W':                                        // vinder: konfetti-fest
      fadeToBlackBy(leds, NUM_LEDS, 18);
      for (byte k = 0; k < 2; k++) leds[random16(NUM_LEDS)] += CHSV(random8(), 200, 255);
      break;
    case 'D': {                                      // drikkepause: lilla fyrværkeri i roligt bastempo
      const int bpm = 72;                            // grundtempo
      const byte pr_slag = 4;                        // raketter pr. slag (flere = mere fyrværkeri)
      const byte daempet = 140;                      // lysstyrke for drikkepausen (0-255)
      #define FW 12                                  // max raketter i luften på én gang
      static int  fwC[FW];                            // hvor raketten springer
      static unsigned long fwT[FW];                   // hvornår
      static byte fwH[FW];                            // farve
      static byte nxt = 0;
      static unsigned long lastBeat = 0, ventetid = 0;
      if (now - lastBeat >= ventetid) {               // ny raket med lidt tilfældig afstand
        lastBeat = now;
        unsigned long gns = 60000UL / bpm / pr_slag;
        ventetid = gns / 2 + random16(gns);
        const byte farver[] = {190, 200, 213, 224};  // lilla, violet, magenta, lyserød
        fwC[nxt] = random16(NUM_LEDS); fwT[nxt] = now; fwH[nxt] = farver[random8() % 4];
        nxt = (nxt + 1) % FW;
      }
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      for (byte k = 0; k < FW; k++) {
        unsigned long age = now - fwT[k];
        if (!fwT[k] || age > 1600) continue;
        float r = 7 * (1 - exp(-(age / 220.0)));    // gnisterne flyver ud til begge sider
        float br = exp(-(age / 450.0));              // og falmer
        for (int i = fwC[k] - (int)r - 1; i <= fwC[k] + (int)r + 1; i++) {
          if (i < 0 || i >= NUM_LEDS) continue;
          float d = abs(i - fwC[k]);
          float v = d <= r ? br * (0.3 + 0.7 * d / (r + 0.01)) : 0;   // stærkest i kanten
          if (age < 120 && d < 1.5) v = 1;                           // knaldet i midten
          if (age > 500 && random8() < 60) v *= 0.15;                // knitren til sidst
          leds[i] += CHSV(fwH[k] + random8(10), 255, v * 255);
        }
      }
      for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(daempet);
      break; }
    case 'M': {                                      // disco når musikken spiller
      unsigned long beat = now / 395;
      for (int i = 0; i < NUM_LEDS; i++) {
        byte seg = i / 6;
        leds[i] = CHSV(seg * 45 + beat * 70, 255, ((seg + beat) % 2) ? 255 : 60);
      }
      break; }
    case 'X':                                        // fast farve
      fill_solid(leds, NUM_LEDS, baseCol); break;
  }
}

// ---------- effekter ----------
void drawFx(unsigned long t) {
  switch (fx) {
    case 'g': {                                      // rigtigt: grøn bølge ud fra midten + glimt
      long front = (long)t * (NUM_LEDS / 2 + 1) / 450;
      byte b = t < 1100 ? 255 : 255 - (t - 1100) * 255 / 600;
      for (int i = 0; i < NUM_LEDS; i++) {
        int d = abs(i - NUM_LEDS / 2);
        leds[i] = d <= front ? CRGB(0, b, b / 6) : CRGB(0, 0, 0);
      }
      if (t > 450 && t < 1300) for (byte k = 0; k < 3; k++) leds[random16(NUM_LEDS)] = CRGB(120, 255, 0);
      break; }
    case 'r':                                        // forkert: rød blinker to gange
      fill_solid(leds, NUM_LEDS, (t % 500) < 300 ? CRGB(255, 0, 0) : CRGB(0, 0, 0)); break;
    case 'b': {                                      // buzz: blinker 3 gange i lilla, lyserød og mørkelilla
      byte blink = t / 400;                          // hvert blink: 250 ms tændt, 150 ms slukket
      if (t % 400 >= 250 || blink > 2) { fill_solid(leds, NUM_LEDS, CRGB::Black); break; }
      CRGB morkLilla(60, 0, 110), lilla(150, 0, 255), lyseroed(255, 20, 140);
      for (int i = 0; i < NUM_LEDS; i++) {
        byte x = sin8(i * 14 + blink * 85);          // blandingen flytter sig lidt for hvert blink
        leds[i] = x < 128 ? blend(morkLilla, lilla, x * 2) : blend(lilla, lyseroed, (x - 128) * 2);
      }
      break; }
    case 'f': {                                      // fanfare: regnbue fejer hen over kæden
      long front = (long)t * NUM_LEDS / 700;
      byte b = t < 1000 ? 255 : 255 - (t - 1000) * 255 / 400;
      for (int i = 0; i < NUM_LEDS; i++)
        leds[i] = i <= front ? CRGB(CHSV(i * 256 / NUM_LEDS + t / 4, 255, b)) : CRGB(0, 0, 0);
      break; }
    case 't':                                        // tik: kort mørkt blink
      drawBase(millis());
      for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(50);
      break;
  }
}

// ALDRIG HVID: hvid trækker mest strøm. Fjerner den hvide del af alle farver,
// så LED'erne kun lyser med mættede farver (også hvis spillet sender en lys farve).
void ingenHvid() {
  for (int i = 0; i < NUM_LEDS; i++) {
    CRGB &c = leds[i];
    byte w = min(c.r, min(c.g, c.b));
    if (w > 40) { byte d = w - 40; c.r -= d; c.g -= d; c.b -= d; }
  }
}

void render() {
  unsigned long now = millis();
  if (fx && now - fxStart < fxLen) drawFx(now - fxStart);
  else { fx = 0; drawBase(now); }
  ingenHvid();
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MA);
  FastLED.setBrightness(LYSSTYRKE);
  FastLED.clear(true);
  fxStart = millis();
}

/*
  Lyset kan ikke opdateres og modtage data på samme tid (data går tabt).
  Derfor: Arduinoen tegner et billede, sender 'k' og venter på at computeren svarer
  med en linje (evt. tom). Først derefter tegnes næste billede.
*/
void loop() {
  unsigned long now = millis();

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      buf[len] = 0; line(buf); len = 0;
      synced = true; waiting = false;
    } else if (c != '\r' && len < sizeof(buf) - 1) buf[len++] = c;
  }

  if (waiting && now - askedAt > 250) {             // computeren svarer ikke -> kør videre alene
    waiting = false; synced = false; len = 0;
  }

  if (!waiting && now - lastFrame >= 16) {
    lastFrame = now;
    render();
    Serial.write('k');
    askedAt = millis();
    waiting = true;
    if (!synced) { waiting = false; delay(14); }      // alene: bare kør animationen
  }
}
