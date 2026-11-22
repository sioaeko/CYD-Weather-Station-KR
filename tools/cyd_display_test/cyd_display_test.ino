/*
 * CYD 화면 진단: 디스플레이 칩 설정 4가지를 6초씩 번갈아 시도합니다.
 * 올바른 설정에서만 "파란 바탕 + 흰 숫자" 로 보입니다. 라이브러리 없이 SPI 로 직접 그립니다.
 *   1 = ILI9341 (BGR, 반전 없음)      ← 초기형 CYD
 *   2 = ILI9341 (BGR, 색 반전)
 *   3 = ST7789  (RGB, 색 반전)         ← USB 2개 CYD 에서 흔함
 *   4 = ST7789  (RGB, 반전 없음)
 * 백라이트(GPIO21)는 PWM 없이 HIGH 로 켭니다.
 */
#include <SPI.h>

constexpr int PIN_MOSI = 13, PIN_MISO = 12, PIN_SCLK = 14, PIN_CS = 15, PIN_DC = 2, PIN_BL = 21;
SPIClass bus(HSPI);

void command(uint8_t c) { digitalWrite(PIN_DC, LOW); digitalWrite(PIN_CS, LOW); bus.transfer(c); digitalWrite(PIN_CS, HIGH); }
void data8(uint8_t d) { digitalWrite(PIN_DC, HIGH); digitalWrite(PIN_CS, LOW); bus.transfer(d); digitalWrite(PIN_CS, HIGH); }
void window(int x0, int y0, int x1, int y1) {
  command(0x2A); data8(x0 >> 8); data8(x0); data8(x1 >> 8); data8(x1);
  command(0x2B); data8(y0 >> 8); data8(y0); data8(y1 >> 8); data8(y1);
  command(0x2C);
}
void fill(int x, int y, int w, int h, uint16_t color) {
  window(x, y, x + w - 1, y + h - 1);
  digitalWrite(PIN_DC, HIGH); digitalWrite(PIN_CS, LOW);
  for (long i = 0; i < long(w) * h; ++i) { bus.transfer(color >> 8); bus.transfer(color); }
  digitalWrite(PIN_CS, HIGH);
}
// 7-세그먼트 숫자 (흰색, 큼직하게)
void digit(int n) {
  const uint8_t seg[] = {0, 0b0110000, 0b1101101, 0b1111001, 0b0110011};  // 1~4: a b c d e f g
  const int x = 70, y = 70, w = 100, t = 18, h = 90;
  const uint8_t s = seg[n];
  if (s & 0b1000000) fill(x, y, w, t, 0xFFFF);                  // a
  if (s & 0b0100000) fill(x + w - t, y, t, h, 0xFFFF);          // b
  if (s & 0b0010000) fill(x + w - t, y + h, t, h, 0xFFFF);      // c
  if (s & 0b0001000) fill(x, y + 2 * h - t, w, t, 0xFFFF);      // d
  if (s & 0b0000100) fill(x, y + h, t, h, 0xFFFF);              // e
  if (s & 0b0000010) fill(x, y, t, h, 0xFFFF);                  // f
  if (s & 0b0000001) fill(x, y + h - t / 2, w, t, 0xFFFF);      // g
}
void init(bool st7789, uint8_t madctl, bool invert) {
  command(0x01); delay(150);             // 소프트웨어 리셋
  command(0x11); delay(120);             // 슬립 해제
  command(0x3A); data8(0x55); delay(10); // 16비트 색
  command(0x36); data8(madctl);
  command(invert ? 0x21 : 0x20);
  if (st7789) { command(0x13); delay(10); }
  command(0x29); delay(50);              // 화면 켜기
}
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BL, OUTPUT); digitalWrite(PIN_BL, HIGH);
  pinMode(PIN_CS, OUTPUT); digitalWrite(PIN_CS, HIGH); pinMode(PIN_DC, OUTPUT);
  bus.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_CS);
  bus.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  Serial.println("\nCYD DISPLAY TEST start (backlight GPIO21 HIGH)");
}
void loop() {
  struct { bool st; uint8_t mad; bool inv; const char* name; } tests[] = {
    {false, 0x48, false, "1 ILI9341 BGR normal"}, {false, 0x48, true, "2 ILI9341 BGR inverted"},
    {true, 0x00, true, "3 ST7789 RGB inverted"}, {true, 0x00, false, "4 ST7789 RGB normal"}};
  for (int i = 0; i < 4; ++i) {
    init(tests[i].st, tests[i].mad, tests[i].inv);
    fill(0, 0, 240, 320, 0x001F);  // 파랑
    digit(i + 1);
    Serial.printf("TEST %s\n", tests[i].name);
    delay(6000);
  }
}
