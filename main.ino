#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Keypad.h>

const char* ssid = "RoboArm";
const char* password = "12345678";

const int pins[4]   = {14, 13, 33, 32};   // s1, s2, s3, s4
const int minA[4]   = {75,  0,  0,  0};
const int maxA[4]   = {95, 180, 180, 180};

#define RELAY_PIN 15
#define RELAY_ON  HIGH
#define RELAY_OFF (!RELAY_ON)

#define PROG_SERVO  3      // s4 (пін 32)
#define PROG_REPEAT 2      // скільки разів повторити
#define PROG_PAUSE  800    // пауза між рухами, мс

// ---------- Клавіатура ----------
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {4, 16, 17, 18}; 
byte colPins[COLS] = {19, 25, 26, 23}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

enum Mode { NORMAL, INPUT1, INPUT2, RUNNING };
Mode mode = NORMAL;
String buf = "";
String msg = "";
int progA1 = 0, progA2 = 0;
int runCycle = 0;

// ---------- Екран ----------
#define LCD_ADDR 0x3F
#define CONTRAST 0x10

GFXcanvas1 canvas(128, 32);
bool lcdDirty = true;

void cmd(uint8_t c) {
  Wire.beginTransmission(LCD_ADDR);
  Wire.write(0x00);
  Wire.write(c);
  Wire.endTransmission();
}

void lcdInit() {
  cmd(0xE2); delay(10);
  cmd(0xA3);
  cmd(0xA0);
  cmd(0xC8);
  cmd(0x22);
  cmd(0x81); cmd(CONTRAST);
  cmd(0x2C); delay(5);
  cmd(0x2E); delay(5);
  cmd(0x2F); delay(5);
  cmd(0x40);
  cmd(0xAF);
}

void lcdShow() {
  for (uint8_t page = 0; page < 4; page++) {
    cmd(0xB0 | page);
    cmd(0x10);
    cmd(0x00);
    for (uint8_t x = 0; x < 128; x += 16) {
      Wire.beginTransmission(LCD_ADDR);
      Wire.write(0x40);
      for (uint8_t i = 0; i < 16; i++) {
        uint8_t b = 0;
        for (uint8_t bit = 0; bit < 8; bit++)
          if (canvas.getPixel(x + i, page * 8 + bit)) b |= (1 << bit);
        Wire.write(b);
      }
      Wire.endTransmission();
    }
  }
}

// ---------- Серво і реле ----------
Servo servos[4];
int pos[4];
bool attached[4] = {false, false, false, false};
bool relayState = false;

WebServer server(80);

String angleText(int i) {
  return attached[i] ? String(pos[i]) : String("--");
}

void line(int row, String text) {
  canvas.setCursor(0, row * 8);
  canvas.print(text);
}

void drawScreen() {
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  canvas.setTextSize(1);

  if (mode == NORMAL) {
    line(0, WiFi.softAPIP().toString());
    line(1, String("Relay: ") + (relayState ? "ON" : "OFF") + "  D=prog");
    canvas.setCursor(0, 16);  canvas.print("S1:" + angleText(0));
    canvas.setCursor(64, 16); canvas.print("S2:" + angleText(1));
    canvas.setCursor(0, 24);  canvas.print("S3:" + angleText(2));
    canvas.setCursor(64, 24); canvas.print("S4:" + angleText(3));
  }
  else if (mode == INPUT1) {
    line(0, "S4 program");
    line(1, "Angle 1: " + buf + "_");
    line(2, "C=OK *=del #=exit");
    line(3, msg);
  }
  else if (mode == INPUT2) {
    line(0, "S4 program");
    line(1, "Angle 1: " + String(progA1));
    line(2, "Angle 2: " + buf + "_");
    line(3, msg.length() ? msg : String("C=OK *=del #=exit"));
  }
  else if (mode == RUNNING) {
    line(0, "S4 running...");
    line(1, "A1:" + String(progA1) + "  A2:" + String(progA2));
    line(2, "Cycle " + String(runCycle) + "/" + String(PROG_REPEAT));
    line(3, "Pos: " + angleText(PROG_SERVO));
  }

  lcdShow();
}

void setRelay(bool on) {
  relayState = on;
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
  lcdDirty = true;
}

void moveServo(int i, int target, int speed) {
  target = constrain(target, minA[i], maxA[i]);

  if (!attached[i]) {
    servos[i].setPeriodHertz(50);
    servos[i].attach(pins[i], 500, 2400);
    servos[i].write(target);
    pos[i] = target;
    attached[i] = true;
  } else if (speed <= 0) {
    servos[i].write(target);
    pos[i] = target;
  } else {
    while (pos[i] != target) {
      pos[i] += (target > pos[i]) ? 1 : -1;
      servos[i].write(pos[i]);
      delay(speed);
    }
  }
  lcdDirty = true;
}

// ---------- Програма з клавіатури ----------
void runProgram() {
  mode = RUNNING;
  for (runCycle = 1; runCycle <= PROG_REPEAT; runCycle++) {
    moveServo(PROG_SERVO, progA1, 0);
    drawScreen();
    delay(PROG_PAUSE);

    moveServo(PROG_SERVO, progA2, 0);
    drawScreen();
    delay(PROG_PAUSE);
  }
  mode = NORMAL;
  lcdDirty = true;
}

void handleKey(char k) {
  if (mode == NORMAL) {
    if (k == 'D') {
      mode = INPUT1;
      buf = "";
      msg = "";
    }
  }
  else if (mode == INPUT1 || mode == INPUT2) {
    if (k >= '0' && k <= '9') {
      if (buf.length() < 3) buf += k;
      msg = "";
    }
    else if (k == '*') {
      if (buf.length()) buf.remove(buf.length() - 1);
    }
    else if (k == '#') {
      mode = NORMAL;
    }
    else if (k == 'C') {
      int v = buf.toInt();
      if (buf.length() == 0) {
        msg = "Enter a number";
      } else if (v < minA[PROG_SERVO] || v > maxA[PROG_SERVO]) {
        msg = "Only " + String(minA[PROG_SERVO]) + "-" + String(maxA[PROG_SERVO]);
        buf = "";
      } else if (mode == INPUT1) {
        progA1 = v;
        buf = "";
        msg = "";
        mode = INPUT2;
      } else {
        progA2 = v;
        buf = "";
        msg = "";
        runProgram();
      }
    }
  }
  lcdDirty = true;
}

// ---------- Вебсторінка ----------
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>RoboArm</title>
<style>
 body{font-family:sans-serif;background:#1e1e1e;color:#eee;max-width:420px;margin:auto;padding:16px}
 .s{background:#2b2b2b;border-radius:10px;padding:12px;margin:10px 0}
 input[type=number]{width:70px;font-size:18px;padding:4px}
 input[type=range]{width:100%}
 button{font-size:18px;padding:4px 14px}
 .on{background:#2e7d32;color:#fff;border:none;border-radius:6px}
 .off{background:#c62828;color:#fff;border:none;border-radius:6px}
 .lim{color:#999;font-size:13px}
 #st{color:#8f8}
</style></head><body>
<h2>RoboArm</h2>

<div class="s"><b>Реле (мотор)</b>: <span id="rs">?</span><br><br>
 <button class="on" onclick="relay(1)">УВІМК</button>
 <button class="off" onclick="relay(0)">ВИМК</button>
</div>

<div class="s">Швидкість (0 = швидко, 10+ = плавно):
 <input type="number" id="sp" min="0" max="50" value="0"></div>
<div id="list"></div>
<p id="st"></p>
<script>
const MIN=[75,0,0,0], MAX=[95,180,180,180];
let h='';
for(let i=1;i<=4;i++){
 let mn=MIN[i-1], mx=MAX[i-1], mid=Math.round((mn+mx)/2);
 h+=`<div class="s"><b>Серво ${i}</b> <span class="lim">(${mn}–${mx}°)</span><br>
 <input type="number" id="n${i}" min="${mn}" max="${mx}" value="${mid}">
 <button onclick="go(${i},n${i}.value)">Go</button>
 <input type="range" id="r${i}" min="${mn}" max="${mx}" value="${mid}"
   oninput="n${i}.value=this.value" onchange="go(${i},this.value)"></div>`;
}
list.innerHTML=h;
function go(s,a){
 a=Math.max(MIN[s-1],Math.min(MAX[s-1],parseInt(a)||0));
 document.getElementById('n'+s).value=a;
 document.getElementById('r'+s).value=a;
 let v=parseInt(sp.value)||0;
 st.textContent='s'+s+' → '+a+'° ...';
 fetch(`/set?s=${s}&a=${a}&v=${v}`).then(r=>r.text()).then(t=>st.textContent=t)
   .catch(()=>st.textContent='Немає зв\'язку');
}
function showRelay(t){ rs.textContent = t=='1' ? 'УВІМКНЕНО' : 'вимкнено';
 rs.style.color = t=='1' ? '#8f8' : '#f88'; }
function relay(on){
 fetch(`/relay?on=${on}`).then(r=>r.text()).then(showRelay)
   .catch(()=>st.textContent='Немає зв\'язку');
}
fetch('/relay').then(r=>r.text()).then(showRelay);
</script></body></html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", PAGE);
}

void handleSet() {
  int s = server.arg("s").toInt();
  int a = server.arg("a").toInt();
  int v = server.arg("v").toInt();
  if (s < 1 || s > 4) {
    server.send(400, "text/plain", "Error");
    return;
  }
  moveServo(s - 1, a, v);
  server.send(200, "text/plain; charset=utf-8",
              "s" + String(s) + " → " + String(pos[s - 1]) + "° OK");
}

void handleRelay() {
  if (server.hasArg("on")) {
    setRelay(server.arg("on").toInt() == 1);
  }
  server.send(200, "text/plain", relayState ? "1" : "0");
}

// ---------- Старт ----------
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);

  Serial.begin(115200);

  Wire.begin(21, 22);
  Wire.setClock(400000);
  lcdInit();

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  WiFi.softAP(ssid, password);
  Serial.print("Wi-Fi: "); Serial.println(ssid);
  Serial.print("Відкрий: http://"); Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/relay", handleRelay);
  server.begin();

  lcdDirty = true;
}

void loop() {
  server.handleClient();

  char k = keypad.getKey();
  if (k) {
    Serial.printf("Key: %c\n", k);
    handleKey(k);
  }

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("r")) {
      setRelay(line.substring(1).toInt() == 1);
      Serial.println(relayState ? "Реле: УВІМК" : "Реле: ВИМК");
    } else {
      int n, angle, speed = 0;
      if (sscanf(line.c_str(), "%d %d %d", &n, &angle, &speed) >= 2 &&
          n >= 1 && n <= 4) {
        moveServo(n - 1, angle, speed);
        Serial.printf("s%d → %d°\n", n, pos[n - 1]);
      }
    }
  }

  if (lcdDirty) {
    lcdDirty = false;
    drawScreen();
  }
}
