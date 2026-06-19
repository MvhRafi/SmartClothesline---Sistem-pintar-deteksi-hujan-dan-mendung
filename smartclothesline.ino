#include <Arduino.h>

// --- PIN SELECTION ---
#define PIN_RAIN_SENSOR  33
#define PIN_LDR_SENSOR   34
#define PIN_LIMIT_KANAN  17  
#define PIN_LIMIT_KIRI   16 
#define PIN_MOTOR_RPWM   26  
#define PIN_MOTOR_LPWM   27 
#define R_EN             14
#define L_EN             13
#define PIN_LED          15

// --- KALIBRASI ---
const int THRESHOLD_RAIN = 2500;  // ADC < nilai ini → Hujan
const int THRESHOLD_LDR = 1800;  // ADC > nilai ini → Malam/Gelap
const int KECEPATAN_MOTOR = 100;   // PWM 0–255
const int BACK_KECEPATAN_MOTOR = 250;
String data_input = "";

const unsigned long DEBOUNCE_CUACA_MS = 100;  // 3 detik
bool serial_ready = false;

enum StatusJemuran {
  BERHENTI,
  BERGERAK_KELUAR,
  TERBENTANG,
  BERGERAK_KEDALAM,
  TEDUH
};

StatusJemuran statusSaatIni = BERHENTI;

bool        cuacaBuruk_pending  = false;  
bool        cuacaBaik_pending   = false;
unsigned long debounce_timer    = 0;

void motorKeLuar(){
  analogWrite(PIN_MOTOR_LPWM, 0);
  analogWrite(PIN_MOTOR_RPWM, KECEPATAN_MOTOR);
}
void motorKeDalam() {
  analogWrite(PIN_MOTOR_RPWM, 0);
  analogWrite(PIN_MOTOR_LPWM, BACK_KECEPATAN_MOTOR);
}
void motorBerhenti() {
  analogWrite(PIN_MOTOR_RPWM, 0);
  analogWrite(PIN_MOTOR_LPWM, 0);
}

const char* namaState(StatusJemuran s) {
  switch (s) {
    case BERHENTI:          return "BERHENTI";
    case BERGERAK_KELUAR:   return "BERGERAK_KELUAR";
    case TERBENTANG:        return "TERBENTANG";
    case BERGERAK_KEDALAM:  return "BERGERAK_KEDALAM";
    case TEDUH:             return "TEDUH";
    default:                return "UNKNOWN";
  }
}

void bacaSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (data_input.length() > 0) {
        data_input.trim();
        serial_ready = true;
      }
    } else {
      data_input += c;
    }
  }
}

void proses() {
  if(!serial_ready) return;

  Serial.print(data_input);

  if (data_input == "0") {
    digitalWrite(PIN_LED, LOW);
    Serial.println("MATAHARI ON");
  } else if (data_input == "1") {
    digitalWrite(PIN_LED, HIGH);
    Serial.println("MATAHARI OFF");
  } else {
    Serial.println("Perintah tidak dikenal. Kirim '0' atau '1'.");
  }

  data_input = "";
  serial_ready = false;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_RAIN_SENSOR, INPUT);
  pinMode(PIN_LDR_SENSOR,  INPUT);
  pinMode(PIN_LIMIT_KANAN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_KIRI,  INPUT_PULLUP);
  pinMode(PIN_MOTOR_RPWM,  OUTPUT);
  pinMode(PIN_MOTOR_LPWM,  OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  pinMode(PIN_LED, OUTPUT);

  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);
  motorBerhenti();

  Serial.println("Smart Clothesline by Idea Dinta Egiluy");
  Serial.println("Motor Stop! menungu sensor membaca");
}

void loop() {
  bacaSerial();
  proses();

  unsigned long now = millis();
  int nilaiHujan = analogRead(PIN_RAIN_SENSOR);
  int nilaiLDR   = analogRead(PIN_LDR_SENSOR);

  bool limitKanan = (digitalRead(PIN_LIMIT_KANAN) == LOW);
  bool limitKiri  = (digitalRead(PIN_LIMIT_KIRI)  == LOW);

  bool isHujan = (nilaiHujan < THRESHOLD_RAIN);
  bool isMalam = (nilaiLDR   < THRESHOLD_LDR);
  bool cuacaBuruk = isHujan || isMalam;

  bool cuacaBurukKonfirmasi = false;
  bool cuacaBaikKonfirmasi  = false;

  if (cuacaBuruk) {
    if (!cuacaBuruk_pending) {
      cuacaBuruk_pending = true;
      cuacaBaik_pending  = false;
      debounce_timer = now;
    }
    if ((now - debounce_timer) >= DEBOUNCE_CUACA_MS) {
      cuacaBurukKonfirmasi = true;
    }
  } else {
    if (!cuacaBaik_pending) {
      cuacaBaik_pending  = true;
      cuacaBuruk_pending = false;
      debounce_timer = now;
    }
    if ((now - debounce_timer) >= DEBOUNCE_CUACA_MS) {
      cuacaBaikKonfirmasi = true;
    }
  }

  StatusJemuran stateSebelum = statusSaatIni;

  switch (statusSaatIni) {
    case BERHENTI:
      motorBerhenti();
      if (cuacaBurukKonfirmasi && !limitKiri) {
        statusSaatIni = BERGERAK_KEDALAM;
      } else if (cuacaBaikKonfirmasi && !limitKanan) {
        statusSaatIni = BERGERAK_KELUAR;
      } else if (limitKiri) {
        statusSaatIni = TEDUH;       
      } else if (limitKanan) {
        statusSaatIni = TERBENTANG;  
      }
      break;

    case BERGERAK_KELUAR:
      motorKeLuar();
      if (limitKanan) {
        motorBerhenti();
        statusSaatIni = TERBENTANG;
      } else if (cuacaBurukKonfirmasi) {
        motorBerhenti();
        statusSaatIni = BERGERAK_KEDALAM;
      }
      break;

    case TERBENTANG:
      motorBerhenti();
      if (cuacaBurukKonfirmasi) {
        statusSaatIni = BERGERAK_KEDALAM;
      }
      break;

    case BERGERAK_KEDALAM:
      motorKeDalam();
      if (limitKiri) {
        motorBerhenti();
        statusSaatIni = TEDUH;
      } else if (cuacaBaikKonfirmasi) {
        motorBerhenti();
        statusSaatIni = BERGERAK_KELUAR;
      }
      break;

    case TEDUH:
      motorBerhenti();
      if (cuacaBaikKonfirmasi) {
        statusSaatIni = BERGERAK_KELUAR;
      }
      break;
  }

  if (statusSaatIni != stateSebelum) {
    Serial.print("[TRANSISI] ");
    Serial.print(namaState(stateSebelum));
    Serial.print(" → ");
    Serial.println(namaState(statusSaatIni));
  }

  Serial.print("Hujan(ADC):");  Serial.print(nilaiHujan);
  Serial.print(isHujan ? "[HUJAN]" : "[KERING]");
  Serial.print(" | LDR(ADC):"); Serial.print(nilaiLDR);
  Serial.print(isMalam ? "[MALAM]" : "[SIANG]");
  Serial.print(" | LS Ki/Ka:"); Serial.print(limitKiri);
  Serial.print("/");            Serial.print(limitKanan);
  Serial.print(" | State: ");   Serial.println(namaState(statusSaatIni));

  delay(200);
}
