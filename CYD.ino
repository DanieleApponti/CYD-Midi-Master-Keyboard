#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// --- CONFIGURAZIONE PIN CYD STANDARD ---
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

#define AUDIO_PIN 26 

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

// --- CALIBRAZIONE TOUCH ---
#define TS_MINX 300
#define TS_MAXX 3800
#define TS_MINY 200
#define TS_MAXY 3800

// --- LOGICA DI GIOCO / MIDI ---
int ottavaBase = 4; 
int ultimaNotaSuonata = -1;
int volumeLocale = 50; // Volume iniziale al 50% (Range da 0 a 100)
float frequenzaAttiva = 0;

const int noteBiancheMIDI[] = {0, 2, 4, 5, 7, 9, 11};
const int noteNereMIDI[]   = {1, 3, 6, 8, 10};

const float frequenzeNOTE[] = {
  130.81, 138.59, 146.83, 155.56, 164.81, 174.61, 185.00, 196.00, 207.65, 220.00, 233.08, 246.94, // Ottava 3
  261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99, 392.00, 415.30, 440.00, 466.16, 493.88, // Ottava 4
  523.25, 554.37, 587.33, 622.25, 659.25, 698.46, 739.99, 783.99, 830.61, 880.00, 932.33, 987.77  // Ottava 5
};

int larghezzaTastoBianco = 45; 
int altezzaTastoBianco = 160;

void setup() {
  Serial.begin(31250); 
  
  // NUOVA SINTASSI ESP32 CORE 3.0+: Configura pin, frequenza iniziale (2000Hz) e risoluzione (8 bit)
  ledcAttach(AUDIO_PIN, 2000, 8); 
  ledcWrite(AUDIO_PIN, 0); // Parte in silenzio
  
  tft.init();
  tft.setRotation(1); 
  
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchscreenSPI);
  touch.setRotation(1);

  disegnaInterfaccia();
}

void loop() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    
    int x = map(p.x, TS_MINX, TS_MAXX, 0, 320);
    int y = map(p.y, TS_MINY, TS_MAXY, 0, 240);

    // 1. AREA DI CONTROLLO SUPERIORE (Y tra 10 e 50)
    if (y > 10 && y < 50) {
      // Tasto OCT -
      if (x > 10 && x < 85) { 
        if (ottavaBase > 3) {  
          ottavaBase--;
          disegnaAreaTesto();
          delay(250); 
        }
        return;
      }
      // Tasto OCT +
      if (x > 235 && x < 310) { 
        if (ottavaBase < 5) {   
          ottavaBase++;
          disegnaAreaTesto();
          delay(250); 
        }
        return;
      }
      // BARRA DEL VOLUME (Toccare dentro l'area centrale tra X=95 e X=225)
      if (x >= 95 && x <= 225) {
        volumeLocale = map(x, 95, 225, 0, 100);
        disegnaBarraVolume();
        
        if (ultimaNotaSuonata != -1) {
          int dutyCycle = map(volumeLocale, 0, 100, 0, 127); 
          ledcWrite(AUDIO_PIN, dutyCycle);
        }
        delay(50);
        return;
      }
    }

    // 2. PRESSIONE TASTI TASTIERA (Area inferiore)
    if (y >= 60 && y <= 220) {
      int notaRilevata = -1;

      // Tasti neri
      if (y < 60 + 100) { 
        if (x > 30 && x < 30 + 30)   notaRilevata = noteNereMIDI[0];
        if (x > 75 && x < 75 + 30)   notaRilevata = noteNereMIDI[1];
        if (x > 165 && x < 165 + 30) notaRilevata = noteNereMIDI[2];
        if (x > 210 && x < 210 + 30) notaRilevata = noteNereMIDI[3];
        if (x > 255 && x < 255 + 30) notaRilevata = noteNereMIDI[4];
      }

      // Tasti bianchi
      if (notaRilevata == -1) {
        int indiceBianco = x / larghezzaTastoBianco;
        if (indiceBianco > 6) indiceBianco = 6;
        notaRilevata = noteBiancheMIDI[indiceBianco];
      }

      int notaMIDI = (ottavaBase * 12) + notaRilevata;

      if (notaMIDI != ultimaNotaSuonata) {
        if (ultimaNotaSuonata != -1) {
          inviaMIDI(0x80, ultimaNotaSuonata, 0); 
        }
        
        inviaMIDI(0x90, notaMIDI, 127); 
        
        int indiceFrequenza = notaMIDI - 48; 
        if (indiceFrequenza >= 0 && indiceFrequenza < 36) {
          frequenzaAttiva = frequenzeNOTE[indiceFrequenza];
          
          // NUOVA SINTASSI: ledcWriteTone passa direttamente sul pin anziché sul canale
          ledcWriteTone(AUDIO_PIN, frequenzaAttiva);
          
          int dutyCycle = map(volumeLocale, 0, 100, 0, 127); 
          ledcWrite(AUDIO_PIN, dutyCycle); 
        }
        
        ultimaNotaSuonata = notaMIDI;
      }
    }
  } else {
    if (ultimaNotaSuonata != -1) {
      inviaMIDI(0x80, ultimaNotaSuonata, 0);
      ledcWrite(AUDIO_PIN, 0); // Mette a 0 il duty cycle
      ultimaNotaSuonata = -1;
    }
  }
}

void inviaMIDI(byte comando, byte nota, byte velocita) {
  Serial.write(comando);   
  Serial.write(nota);      
  Serial.write(velocita);  
}

void disegnaInterfaccia() {
  tft.fillScreen(TFT_BLACK);

  tft.fillRoundRect(10, 10, 75, 40, 5, TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.drawCentreString("OCT-", 47, 22, 2);

  tft.fillRoundRect(235, 10, 75, 40, 5, TFT_GREEN);
  tft.setTextColor(TFT_BLACK);
  tft.drawCentreString("OCT+", 272, 22, 2);

  disegnaAreaTesto();
  disegnaBarraVolume();

  for (int i = 0; i < 7; i++) {
    tft.fillRect(i * larghezzaTastoBianco, 60, larghezzaTastoBianco - 2, altezzaTastoBianco, TFT_WHITE);
  }
  tft.fillRect(30, 60, 30, 100, TFT_BLACK);  
  tft.fillRect(75, 60, 30, 100, TFT_BLACK);  
  tft.fillRect(165, 60, 30, 100, TFT_BLACK); 
  tft.fillRect(210, 60, 30, 100, TFT_BLACK); 
  tft.fillRect(255, 60, 30, 100, TFT_BLACK); 
}

void disegnaAreaTesto() {
  tft.fillRect(95, 10, 130, 18, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  String testo = "OCT: " + String(ottavaBase - 1); 
  tft.drawCentreString(testo, 160, 12, 1); 
}

void disegnaBarraVolume() {
  tft.fillRect(95, 32, 130, 15, TFT_DARKGREY);
  int larghezzaRiempimento = map(volumeLocale, 0, 100, 0, 130);
  tft.fillRect(95, 32, larghezzaRiempimento, 15, TFT_CYAN);
  tft.drawRect(95, 32, 130, 15, TFT_WHITE); 
}