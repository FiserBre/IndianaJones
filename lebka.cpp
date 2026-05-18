#include <OneWireHub.h>
#include <DS2408.h>
#include <Servo.h>

const int ONEWIRE_PIN = 3; 
OneWireHub hub(ONEWIRE_PIN);
DS2408 ds2408(0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00); 

const int pinySenzoru[] = {A0, A1, A2};
const int pinLED_PWM = 6;              
const int pinZamek = 11;               
const int pinServo = 9;                
const int pinReleLebka = 7;            

const int dobaOtevreniOneWire = 1000;
const int casDoOtevreniKrystaly = 7000;
const int dobaCekaniAkceKrystaly = 1500;
const int rychlostFading = 24;

const int stredKlid = 510;           
const int toleranceKlid = 15;        
const int limitAktivace = 450;       
const int toleranceOdskoku = 45;
const int dobaUstaleni = 500;        
const int dobaPotvrzeniCile = 200;   

Servo mojeServo;

enum StavSenzoru { NEZNAMY, KLID, PRIBLIZOVANI, AKTIVNI_PLYNLULE, BLOKOVAN_CHAOS };
StavSenzoru stavy[3] = {NEZNAMY, NEZNAMY, NEZNAMY};

int nejnizsiHodnota[3];
unsigned long casPoslednihoPoklesu[3];
unsigned long casZacatkuKlidu[3];

bool sekvenceKrystalyBezi = false;
bool cekaniNaUvolneniSenzoru = false;

bool oneWireAktivniPraveTed = false; 
unsigned long casStartuOneWire = 0;
unsigned long casSekvenceKrystaly = 0;

void setup() {
  Serial.begin(115200); 
  
  pinMode(pinLED_PWM, OUTPUT);
  pinMode(pinZamek, OUTPUT);
  pinMode(pinReleLebka, OUTPUT);
  digitalWrite(pinZamek, LOW);
  digitalWrite(pinReleLebka, LOW);

  mojeServo.attach(pinServo);
  mojeServo.write(180);
  delay(800);
  mojeServo.detach();

  hub.attach(ds2408);
  ds2408.setPinState(0, true); 
  ds2408.setPinState(1, true); 

  Serial.println("--- LEBKA: 7s ZPOZDENI, 6s FADING, TOLERANCE 45 ---");
}

void loop() {
  unsigned long ted = millis();
  hub.poll();

  if (ds2408.getPinState(1) == false && !oneWireAktivniPraveTed) {
    oneWireAktivniPraveTed = true;
    casStartuOneWire = ted;
    
    digitalWrite(pinZamek, HIGH);      
    digitalWrite(pinReleLebka, HIGH);  
    ds2408.setPinState(1, true);
    
    Serial.println(">>> ONEWIRE: Prijato. Oteviram ZAMEK hned (1s).");
  }

  if (oneWireAktivniPraveTed && (ted - casStartuOneWire >= dobaOtevreniOneWire)) {
    oneWireAktivniPraveTed = false;
    if (!sekvenceKrystalyBezi) digitalWrite(pinZamek, LOW);
    Serial.println(">>> ONEWIRE: Zamek vypnut.");
  }

  bool vsechnyKrystalyOk = true;
  for (int i = 0; i < 3; i++) {
    int akt = analogRead(pinySenzoru[i]);
    if (akt > stredKlid + toleranceKlid + 20 && stavy[i] == PRIBLIZOVANI) {
        stavy[i] = BLOKOVAN_CHAOS;
        casZacatkuKlidu[i] = 0;
    }
    switch (stavy[i]) {
      case NEZNAMY:
      case BLOKOVAN_CHAOS:
        if (akt >= stredKlid - toleranceKlid && akt <= stredKlid + toleranceKlid) {
            if (casZacatkuKlidu[i] == 0) casZacatkuKlidu[i] = ted;
            if (ted - casZacatkuKlidu[i] >= dobaUstaleni) stavy[i] = KLID;
        } else { casZacatkuKlidu[i] = 0; }
        break;
      case KLID:
        if (akt < stredKlid - toleranceKlid) {
            stavy[i] = PRIBLIZOVANI;
            nejnizsiHodnota[i] = akt;
            casPoslednihoPoklesu[i] = ted;
        }
        break;
      case PRIBLIZOVANI:
        if (akt > nejnizsiHodnota[i] + toleranceOdskoku) {
            stavy[i] = BLOKOVAN_CHAOS;
            casZacatkuKlidu[i] = 0;
        } else {
            if (akt < nejnizsiHodnota[i]) { nejnizsiHodnota[i] = akt; casPoslednihoPoklesu[i] = ted; }
            if (akt <= limitAktivace) {
                if (ted - casPoslednihoPoklesu[i] >= dobaPotvrzeniCile) stavy[i] = AKTIVNI_PLYNLULE;
            }
        }
        break;
      case AKTIVNI_PLYNLULE:
        if (akt > limitAktivace + toleranceOdskoku) {
            stavy[i] = NEZNAMY;
            casZacatkuKlidu[i] = 0;
            cekaniNaUvolneniSenzoru = false;
        }
        break;
    }
    if (stavy[i] != AKTIVNI_PLYNLULE) vsechnyKrystalyOk = false;
  }

  ds2408.setPinState(0, vsechnyKrystalyOk ? false : true);

  if (vsechnyKrystalyOk && !sekvenceKrystalyBezi && !cekaniNaUvolneniSenzoru) {
    sekvenceKrystalyBezi = true;
    casSekvenceKrystaly = ted;
    Serial.println(">>> KRYSTALY POTVRZENY: Zahajuji 7s odpočet.");
  }

  bool zamekKrystalyZada = false;
  if (sekvenceKrystalyBezi) {
    unsigned long uplynulo = ted - casSekvenceKrystaly;
    
    if (uplynulo >= casDoOtevreniKrystaly && uplynulo < casDoOtevreniKrystaly + dobaCekaniAkceKrystaly) {
      if (!mojeServo.attached()) mojeServo.attach(pinServo);
      zamekKrystalyZada = true; 
      mojeServo.write(30);   
    } 
    else if (uplynulo >= casDoOtevreniKrystaly + dobaCekaniAkceKrystaly && uplynulo < casDoOtevreniKrystaly + dobaCekaniAkceKrystaly + 800) {
      mojeServo.write(180);
    }
    else if (uplynulo >= casDoOtevreniKrystaly + dobaCekaniAkceKrystaly + 800) {
      if (mojeServo.attached()) mojeServo.detach();
      sekvenceKrystalyBezi = false;
      cekaniNaUvolneniSenzoru = true; 
    }
  }

  if (oneWireAktivniPraveTed || zamekKrystalyZada) {
    digitalWrite(pinZamek, HIGH);
  } else {
    digitalWrite(pinZamek, LOW);
  }

  static int jas = 0;
  static unsigned long lastF = 0;
  bool efektSvetla = (vsechnyKrystalyOk || sekvenceKrystalyBezi);
  
  if (ted - lastF >= rychlostFading) {
    if (efektSvetla && jas < 255) jas++;
    else if (!efektSvetla && jas > 0) jas--;
    analogWrite(pinLED_PWM, jas);
    lastF = ted;
  }
}
