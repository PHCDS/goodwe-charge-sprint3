// GoodWe Charge - Sprint 3
// Carregador de carro eletrico simulado no Wokwi (ESP32)
// Publica o status via MQTT e obedece comandos de parada vindos do painel

#include <WiFi.h>
#include <PubSubClient.h>

// ---------- configuracoes ----------
const char* SSID   = "Wokwi-GUEST";   // rede do simulador
const char* SENHA  = "";
const char* BROKER = "broker.hivemq.com";
const int   PORTA  = 1883;

const char* ID_CARREGADOR  = "CHG-01";
const char* TOPICO_STATUS  = "goodwe/fiap1cc/carregador01/status";
const char* TOPICO_COMANDO = "goodwe/fiap1cc/carregador01/comando";

// pinos
const int PINO_BATERIA   = 34;  // potenciometro 1
const int PINO_SOLAR     = 35;  // potenciometro 2
const int LED_CARREGANDO = 2;   // verde
const int LED_PARADO     = 4;   // vermelho
const int BOTAO          = 15;  // plugue conectado

// regras
const float POTENCIA_KW    = 7.4;  // wallbox AC comum
const float SOLAR_MINIMO   = 1.5;  // abaixo disso a energia vem da rede
const int   INTERVALO_MS   = 2000;

WiFiClient wifi;
PubSubClient mqtt(wifi);

bool sessaoAtiva = false;
bool carregando  = false;
unsigned long ultimoEnvio = 0;
unsigned long ultimoBotao = 0;

void conectarWiFi() {
  Serial.print("Conectando no WiFi");
  WiFi.begin(SSID, SENHA);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println(" ok");
}

// chamada toda vez que chega um comando do painel ou do script Python
void aoReceberComando(char* topico, byte* mensagem, unsigned int tamanho) {
  String texto = "";
  for (unsigned int i = 0; i < tamanho; i++) {
    texto += (char) mensagem[i];
  }
  Serial.print("Comando recebido: ");
  Serial.println(texto);

  if (texto.indexOf("parar") >= 0) {
    carregando = false;
    Serial.println(">> carregamento interrompido remotamente");
  }
  if (texto.indexOf("iniciar") >= 0 && sessaoAtiva) {
    carregando = true;
    Serial.println(">> carregamento retomado");
  }
}

void conectarBroker() {
  while (!mqtt.connected()) {
    Serial.print("Conectando no broker MQTT...");
    String cliente = "goodwe-" + String(random(1000, 9999));
    if (mqtt.connect(cliente.c_str())) {
      Serial.println(" ok");
      mqtt.subscribe(TOPICO_COMANDO);
    } else {
      Serial.print(" falhou, codigo ");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

int lerBateria() {
  int leitura = analogRead(PINO_BATERIA);
  return map(leitura, 0, 4095, 0, 100);
}

float lerSolar() {
  int leitura = analogRead(PINO_SOLAR);
  return map(leitura, 0, 4095, 0, 100) / 10.0;  // 0 a 10 kW
}

void verificarBotao() {
  if (digitalRead(BOTAO) == LOW && millis() - ultimoBotao > 400) {
    ultimoBotao = millis();
    sessaoAtiva = !sessaoAtiva;
    carregando = sessaoAtiva;
    if (sessaoAtiva) {
      Serial.println("Plugue conectado, sessao iniciada");
    } else {
      Serial.println("Plugue removido, sessao encerrada");
    }
  }
}

void publicarStatus(int bateria, float solar) {
  String fonte = (solar >= SOLAR_MINIMO) ? "solar" : "rede";
  String estado;
  if (!sessaoAtiva) {
    estado = "disponivel";
  } else if (carregando) {
    estado = "carregando";
  } else {
    estado = "parado";
  }

  char json[220];
  sprintf(json,
    "{\"id\":\"%s\",\"bateria\":%d,\"solar_kw\":%.1f,\"fonte\":\"%s\",\"status\":\"%s\",\"potencia_kw\":%.1f}",
    ID_CARREGADOR, bateria, solar, fonte.c_str(), estado.c_str(), carregando ? POTENCIA_KW : 0.0);

  mqtt.publish(TOPICO_STATUS, json);
  Serial.println(json);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_CARREGANDO, OUTPUT);
  pinMode(LED_PARADO, OUTPUT);
  pinMode(BOTAO, INPUT_PULLUP);

  conectarWiFi();
  mqtt.setServer(BROKER, PORTA);
  mqtt.setCallback(aoReceberComando);
  conectarBroker();
  Serial.println("Carregador pronto. Aperte o botao para conectar o plugue.");
}

void loop() {
  if (!mqtt.connected()) {
    conectarBroker();
  }
  mqtt.loop();
  verificarBotao();

  int bateria = lerBateria();
  float solar = lerSolar();

  // bateria cheia encerra a carga sem depender do painel
  if (bateria >= 100) {
    carregando = false;
  }

  digitalWrite(LED_CARREGANDO, carregando ? HIGH : LOW);
  digitalWrite(LED_PARADO, (sessaoAtiva && !carregando) ? HIGH : LOW);

  if (millis() - ultimoEnvio > INTERVALO_MS) {
    ultimoEnvio = millis();
    publicarStatus(bateria, solar);
  }
}
