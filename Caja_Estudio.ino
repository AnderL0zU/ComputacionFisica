#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <Servo.h>
#include <HX711.h>
#include <LiquidCrystal_I2C.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>


const byte PIN_BT_RX = 2;               // TXD  Bluetooth
const byte PIN_BT_TX = 3;               // RXD Bluetooth
const byte PIN_HX_DT = 4;
const byte PIN_HX_SCK = 5;
const byte PIN_TAPA = 6;                // Sensor magnetico 
const byte PIN_SERVO = 7;
const byte PIN_RFID_RST = 9;
const byte PIN_RFID_SS = 10;


const int ANGULO_CERRADO = 30;
const int ANGULO_ABIERTO = 100;

float factorCalibracion = -420.0;   // factor de calibracion
const float PESO_MINIMO = 15.0;         
const float PESO_SALE = 10.0;           

const long VELOCIDAD_BT = 38400;

String tarjeta1 = "DD 91 14 06";
String tarjeta2 = "E1 E3 CA 06";


MFRC522DriverPinSimple pinSS(PIN_RFID_SS);
MFRC522DriverSPI driver(pinSS, SPI, SPISettings(100000UL, MSBFIRST, SPI_MODE0));
MFRC522 lector(driver);
Servo servo;
HX711 bascula;
SoftwareSerial bt(PIN_BT_RX, PIN_BT_TX);
LiquidCrystal_I2C *lcd;


//  ESTADOS DE LA CAJA
const int VACIAR_BASE = 0;              
const int SIN_CONFIG = 1;               
const int LISTA = 2;                    
const int EN_CURSO = 3;                 
const int EN_PAUSA = 4;                 
const int META_CUMPLIDA = 5;            
const int LIMITE = 6;                   

int estado = VACIAR_BASE;



bool esTemporizador = true;
int minutosMeta = 0;

unsigned long segundosAcumulados = 0;   // Tiempo de estudio ya contado
unsigned long inicioTramo = 0;          // millis() cuando empezo a correr
bool corriendo = false;
int pausas = 0;

int anguloServo = ANGULO_ABIERTO;
float peso = 0;
bool hayObjeto = false;
bool errorBascula = false;
unsigned long ultimaLecturaBascula = 0;
int fallosBascula = 0;            // Lecturas seguidas sin respuesta

String resumen = "";          
bool resumenPendiente = false; 

unsigned long ultimoEnvio = 0;
unsigned long ultimaPantalla = 0;
unsigned long ultimaTarjeta = 0;
String filaAnterior1 = "";
String filaAnterior2 = "";

// Direcciones en la EEPROM 
const int EEPROM_ANGULO = 0;
const int EEPROM_PENDIENTE = 1;
const int EEPROM_RESUMEN = 2;           


// Segundos estudiados hasta ahora
unsigned long segundosEstudio() {
  if (corriendo) {
    return segundosAcumulados + (millis() - inicioTramo) / 1000;
  }
  return segundosAcumulados;
}

void empezarTiempo() {
  corriendo = true;
  inicioTramo = millis();
}

void pararTiempo() {
  if (corriendo) {
    segundosAcumulados = segundosAcumulados + (millis() - inicioTramo) / 1000;
    corriendo = false;
  }
}

String textoTiempo(unsigned long segundos) { // Convierte segundos en texto "hh:mm:ss"
  int h = segundos / 3600;
  int m = (segundos / 60) % 60;
  int s = segundos % 60;
  String texto = "";
  if (h < 10) texto += "0";
  texto += String(h) + ":";
  if (m < 10) texto += "0";
  texto += String(m) + ":";
  if (s < 10) texto += "0";
  texto += String(s);
  return texto;
}

//  PANTALLA LCD

// Escribe las dos filas 
void mostrar(String fila1, String fila2) {
  while (fila1.length() < 16) fila1 += " ";
  while (fila2.length() < 16) fila2 += " ";
  if (fila1 == filaAnterior1 && fila2 == filaAnterior2) return;
  filaAnterior1 = fila1;
  filaAnterior2 = fila2;
  if (lcd != NULL) {
    lcd->setCursor(0, 0);
    lcd->print(fila1.substring(0, 16));
    lcd->setCursor(0, 1);
    lcd->print(fila2.substring(0, 16));
  }
}


void mostrarUnMomento(String fila1, String fila2, int milisegundos) {
  mostrar(fila1, fila2);
  delay(milisegundos);
}


String loQueFalta() { // Validacion
  if (!hayObjeto) return F("Guarda el objeto");
  if (digitalRead(PIN_TAPA) == HIGH) return F("Cierra la tapa");
  return F("Pasa tu tarjeta");
}

void actualizarPantalla() {
  if (errorBascula && estado != VACIAR_BASE) {
    mostrar(F("Error de bascula"), F("Revisa conexion"));
    return;
  }

  unsigned long seg = segundosEstudio();
  unsigned long metaSeg = (unsigned long)minutosMeta * 60;

  //logica estados
  if (estado == VACIAR_BASE) {
    mostrar(F("Vacia la base"), F("y pasa tarjeta"));
  }
  else if (estado == SIN_CONFIG) {
    mostrar(F("Configura sesion"), F("desde la app"));
  }
  else if (estado == LISTA) {
    if (esTemporizador) mostrar("Meta: " + textoTiempo(metaSeg), loQueFalta());
    else mostrar(F("Modo cronometro"), loQueFalta());
  }
  else if (estado == EN_CURSO) {
    if (seg > metaSeg) seg = metaSeg;
    if (esTemporizador) mostrar("Resta: " + textoTiempo(metaSeg - seg), "Pausas: " + String(pausas));
    else mostrar("Tiempo: " + textoTiempo(seg), "Pausas: " + String(pausas));
  }
  else if (estado == EN_PAUSA) {
    if (esTemporizador) mostrar("PAUSA R:" + textoTiempo(metaSeg - seg), loQueFalta());
    else mostrar("PAUSA T:" + textoTiempo(seg), loQueFalta());
  }
  else if (estado == META_CUMPLIDA) {
    mostrar(F("Meta cumplida!"), "Extra: " + textoTiempo(seg - metaSeg));
  }
  else if (estado == LIMITE) {
    mostrar(F("Limite alcanzado"), F("Tiempo: 24:00:00"));
  }
}


void moverServo(int destino) {
  servo.attach(PIN_SERVO);
  servo.write(destino);
  anguloServo = destino;
  delay(700);
  servo.detach();
  EEPROM.update(EEPROM_ANGULO, anguloServo);
}

void abrirPestillo() {
  Serial.println(F("Pestillo: ABRIR"));
  moverServo(ANGULO_ABIERTO);
}

void cerrarPestillo() {
  Serial.println(F("Pestillo: CERRAR"));
  moverServo(ANGULO_CERRADO);
}



void leerBascula() {
  if (millis() - ultimaLecturaBascula < 300) return;   // Lee cada 0,3 s
  ultimaLecturaBascula = millis();

  if (bascula.is_ready()) {
    peso = bascula.get_units(3);
    fallosBascula = 0;
    errorBascula = false;

    if (estado == VACIAR_BASE) return;   // Aun no esta en cero

    if (!hayObjeto && peso >= PESO_MINIMO) {
      hayObjeto = true;
      Serial.println(F(">>> OBJETO DETECTADO"));
    }
    if (hayObjeto && peso < PESO_SALE) {
      hayObjeto = false;
      Serial.println(F(">>> OBJETO RETIRADO"));
    }
  }
  else {
    fallosBascula++;
    if (fallosBascula == 7) {   // 7 lecturas sin respuesta
      errorBascula = true;      
      bt.print(F("ERR,Error de bascula: revisa la conexion\n"));
    }
  }
}

void ponerBasculaEnCero() {
  mostrar(F("Poniendo en cero"), F("No toques nada"));
  bascula.tare(20);
  peso = 0;
  hayObjeto = false;
}

// Revisa si se puede cerrar. Si no, muestra que falta.
bool sePuedeCerrar() {
  if (errorBascula) {
    mostrarUnMomento(F("Error de bascula"), F("Revisa conexion"), 2000);
    return false;
  }
  if (!hayObjeto) {
    mostrarUnMomento(F("Caja vacia"), F("Coloca el objeto"), 2000);
    return false;
  }
  if (digitalRead(PIN_TAPA) == HIGH) {   // HIGH = iman lejos = tapa abierta
    mostrarUnMomento(F("Tapa abierta"), F("Cierra la tapa"), 2000);
    return false;
  }
  return true;
}

//  RESUMEN GUARDADO EN EEPROM 
void guardarResumen() {
  resumenPendiente = true;
  for (unsigned int i = 0; i < 100; i++) {
    char letra = (i < resumen.length()) ? resumen[i] : '\0';
    EEPROM.update(EEPROM_RESUMEN + i, letra);
    if (letra == '\0') break;
  }
  EEPROM.update(EEPROM_PENDIENTE, 1);
}

void cargarResumen() {
  if (EEPROM.read(EEPROM_PENDIENTE) != 1) return;
  resumen = "";
  for (int i = 0; i < 100; i++) {
    char letra = EEPROM.read(EEPROM_RESUMEN + i);
    if (letra == '\0') break;
    resumen += letra;
  }
  resumenPendiente = true;
}




void terminarSesion() {
  pararTiempo();
  unsigned long seg = segundosEstudio();
  if (estado == LIMITE) seg = 86400;//24h

  
  if (esTemporizador) { //resumen
    unsigned long metaSeg = (unsigned long)minutosMeta * 60;
    unsigned long extra = 0;
    if (seg > metaSeg) extra = seg - metaSeg;
    resumen = "Temporizador " + String(minutosMeta) + " min | Estudio " + textoTiempo(seg) +
              " | Extra " + textoTiempo(extra) + " | Pausas " + String(pausas);
  } else {
    resumen = "Cronometro | Tiempo " + textoTiempo(seg) + " | Pausas " + String(pausas);
  }

  guardarResumen();
  bt.print("FIN," + resumen + "\n");

  abrirPestillo();
  estado = SIN_CONFIG;
  segundosAcumulados = 0;
  pausas = 0;
  mostrarUnMomento(F("Sesion terminada"), F("Consulta la app"), 4000);
}


void revisarTiempo() {
  if (estado != EN_CURSO) return;
  unsigned long seg = segundosEstudio();

  if (esTemporizador && seg >= (unsigned long)minutosMeta * 60) {
    estado = META_CUMPLIDA;   // Sigue corriendo: es tiempo extra
  }
  if (!esTemporizador && seg >= 86400) {   // 24 horas
    pararTiempo();
    estado = LIMITE;
  }
}




//  BLUETOOTH
void enviarEstado() {
  String nombre = F("-");
  if (estado == VACIAR_BASE)   nombre = F("Vacia la base y pasa tarjeta");
  if (estado == SIN_CONFIG)    nombre = F("Sin configurar");
  if (estado == LISTA)         nombre = F("Lista para empezar");
  if (estado == EN_CURSO)      nombre = F("En curso");
  if (estado == EN_PAUSA)      nombre = F("En pausa");
  if (estado == META_CUMPLIDA) nombre = F("Meta cumplida");
  if (estado == LIMITE)        nombre = F("Limite alcanzado");

  unsigned long seg = segundosEstudio();
  unsigned long metaSeg = (unsigned long)minutosMeta * 60;
  String tiempo = "-";
  if (estado == LISTA && esTemporizador) tiempo = "Meta " + textoTiempo(metaSeg);
  if (estado == LISTA && !esTemporizador) tiempo = "00:00:00";
  if (estado >= EN_CURSO) {
    if (!esTemporizador) tiempo = textoTiempo(seg);
    else if (seg < metaSeg) tiempo = "Resta " + textoTiempo(metaSeg - seg);
    else tiempo = "Extra " + textoTiempo(seg - metaSeg);
  }

  String textoPausas = "-";
  if (estado >= EN_CURSO) textoPausas = String(pausas);

  bt.print("EST,Estado: " + nombre + ",Tiempo: " + tiempo + ",Pausas: " + textoPausas + "\n");
}

void revisarBluetooth() {
  // Estado cada segundo
  if (millis() - ultimoEnvio >= 1000) {
    ultimoEnvio = millis();
    enviarEstado();
  }

  if (!bt.available()) return;
  String orden = bt.readStringUntil('\n');
  orden.trim();
  if (orden.length() == 0) return;
  char letra = toupper(orden[0]);

  if (letra == 'S') {                 // La app se conecto: estado + resumen pendiente
    enviarEstado();
    if (resumenPendiente) bt.print("FIN," + resumen + "\n");
  }
  else if (letra == 'A') {            // La app guardo el resumen
    resumenPendiente = false;
    EEPROM.update(EEPROM_PENDIENTE, 0);
  }
  else if (estado == VACIAR_BASE) {
    bt.print(F("NO,Primero vacia la base y pasa la tarjeta\n"));
  }
  else if (letra == 'T' || letra == 'C') {
    if (estado != SIN_CONFIG && estado != LISTA) {
      bt.print(F("NO,Hay una sesion en curso\n"));
      return;
    }
    if (letra == 'T') {
      int minutos = orden.substring(1).toInt();
      if (minutos < 1 || minutos > 240) {
        bt.print(F("NO,Los minutos deben estar entre 1 y 240\n"));
        return;
      }
      esTemporizador = true;
      minutosMeta = minutos;
    } else {
      esTemporizador = false;
      minutosMeta = 0;
    }
    estado = LISTA;
    enviarEstado();
  }
  else if (letra == 'F') {
    if (estado >= EN_CURSO) terminarSesion();
    else if (estado == LISTA) {
      estado = SIN_CONFIG;
      bt.print(F("NO,Configuracion cancelada\n"));
    }
    else bt.print(F("NO,No hay una sesion para finalizar\n"));
  }
  else if (letra == 'P' || letra == 'R') {
    bt.print(F("NO,Usa la tarjeta para pausar y continuar\n"));
  }
  else {
    bt.print(F("NO,Orden no valida\n"));
  }
}


//  TARJETA RFID
void revisarTarjeta() {
  if (millis() - ultimaTarjeta < 1500) return;   // Evita leer dos veces seguidas
  if (!lector.PICC_IsNewCardPresent()) return;
  if (!lector.PICC_ReadCardSerial()) return;

  // Pasa el UID a texto
  String uid = "";
  for (byte i = 0; i < lector.uid.size; i++) {
    if (i > 0) uid += " ";
    if (lector.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(lector.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  lector.PICC_HaltA();
  ultimaTarjeta = millis();

  Serial.print(F("UID: "));
  Serial.println(uid);

  if (uid != tarjeta1 && uid != tarjeta2) {
    Serial.println(F("ACCESO DENEGADO"));
    mostrarUnMomento(F("Acceso denegado"), F("Tarjeta invalida"), 2000);
    return;
  }

  // lo que hace la tarjeta segun el estado ----
  if (estado == VACIAR_BASE) {
    Serial.println(F("Base vacia confirmada"));
    ponerBasculaEnCero();
    estado = SIN_CONFIG;
  }
  else if (estado == SIN_CONFIG) {
    mostrarUnMomento(F("Falta configurar"), F("Usa la app"), 2000);
  }
  else if (estado == LISTA) {                 // EMPIEZA
    if (!sePuedeCerrar()) return;
    mostrar(F("Cerrando..."), F("Inicia el tiempo"));
    cerrarPestillo();
    segundosAcumulados = 0;
    pausas = 0;
    empezarTiempo();
    estado = EN_CURSO;
  }
  else if (estado == EN_CURSO) {              // PAUSA
    pararTiempo();
    pausas++;
    mostrar(F("Abriendo..."), F("Tiempo en pausa"));
    abrirPestillo();
    estado = EN_PAUSA;
  }
  else if (estado == EN_PAUSA) {              // CONTINUA
    if (!sePuedeCerrar()) return;
    mostrar(F("Cerrando..."), F("Sigue el tiempo"));
    cerrarPestillo();
    empezarTiempo();
    estado = EN_CURSO;
  }
  else if (estado == META_CUMPLIDA || estado == LIMITE) {   // TERMINA
    terminarSesion();
  }
}


//  MONITOR SERIE: calibrar (c) y poner en cero (t)
void revisarMonitorSerie() {
  if (!Serial.available()) return;
  char letra = tolower(Serial.read());
  delay(50);
  while (Serial.available()) Serial.read();   // Limpia el resto

  if (estado >= EN_CURSO && (letra == 'c' || letra == 't')) {
    Serial.println(F("No se puede durante una sesion."));
    return;
  }

  if (letra == 't') {
    ponerBasculaEnCero();
    if (estado == VACIAR_BASE) estado = SIN_CONFIG;
    Serial.println(F("Bascula en cero."));
  }

  if (letra == 'c') {
    mostrar(F("Calibrando..."), F("Usa monitor"));
    Serial.println(F("1) Quita todo de la plataforma y envia cualquier tecla."));
    while (!Serial.available()) {}
    delay(50);
    while (Serial.available()) Serial.read();
    bascula.set_scale(1.0);
    bascula.tare(20);

    Serial.println(F("2) Pon un objeto de peso conocido y escribe su peso en gramos:"));
    while (!Serial.available()) {}
    float pesoConocido = Serial.parseFloat();
    while (Serial.available()) Serial.read();

    if (pesoConocido > 0) {
      factorCalibracion = bascula.get_value(20) / pesoConocido;
      Serial.print(F("Nuevo factor: "));
      Serial.println(factorCalibracion, 2);
      Serial.println(F("Copialo en 'factorCalibracion' y vuelve a subir el codigo."));
    } else {
      Serial.println(F("Valor no valido."));
    }
    bascula.set_scale(factorCalibracion);
    hayObjeto = false;
    if (estado == VACIAR_BASE) estado = SIN_CONFIG;
  }
}


void setup() {
  Serial.begin(115200);
  bt.begin(VELOCIDAD_BT);
  bt.setTimeout(50);
  pinMode(PIN_TAPA, INPUT_PULLUP);

  // Abrir el pestillo siempre al encender
  if (EEPROM.read(EEPROM_ANGULO) == ANGULO_CERRADO) anguloServo = ANGULO_CERRADO;
  else anguloServo = ANGULO_ABIERTO;
  moverServo(ANGULO_ABIERTO);

  cargarResumen();

  // Pantallabusca su direccion I2C sola
  Wire.begin();
  Wire.setWireTimeout(3000, true);
  byte direccion = 0;
  for (byte d = 8; d < 120; d++) {
    Wire.beginTransmission(d);
    if (Wire.endTransmission() == 0) { direccion = d; break; }
  }
  if (direccion != 0) {
    lcd = new LiquidCrystal_I2C(direccion, 16, 2);
    lcd->init();
    lcd->backlight();
  } else {
    Serial.println(F("AVISO: no encuentro la pantalla."));
  }
  mostrar(F("CAJA DE ESTUDIO"), F("Iniciando..."));

  // Lector RFID
  pinMode(PIN_RFID_RST, OUTPUT);
  digitalWrite(PIN_RFID_RST, LOW);
  delay(10);
  digitalWrite(PIN_RFID_RST, HIGH);
  delay(50);
  lector.PCD_Init();
  byte version = (byte)lector.PCD_GetVersion();
  if (version == 0x00 || version == 0xFF) {
    Serial.println(F("ERROR: RC522 sin comunicacion. Revisa 3.3V, GND y divisores."));
    mostrar(F("Error de RFID"), F("Revisa conexion"));
    bt.print(F("ERR,Error de RFID: revisa la conexion\n"));
    while (true) {}   // Se detiene aqui
  }

  // 5) Bascula (se pone en cero cuando pasen la tarjeta)
  bascula.begin(PIN_HX_DT, PIN_HX_SCK);
  bascula.set_scale(factorCalibracion);
  ultimaLecturaBascula = millis();

  delay(1000);
  Serial.println(F("Comandos: c = calibrar, t = poner bascula en cero"));
}


void loop() {
  revisarMonitorSerie();
  leerBascula();
  revisarTiempo();
  revisarTarjeta();
  revisarBluetooth();

  if (millis() - ultimaPantalla >= 250) {
    ultimaPantalla = millis();
    actualizarPantalla();
  }
}
