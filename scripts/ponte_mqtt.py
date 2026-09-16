"""
GoodWe Charge - Sprint 3
Recebe o status do carregador pelo MQTT, guarda o historico da sessao em CSV
e manda parar o carregamento quando a bateria chega no limite.

Instalar antes: pip install paho-mqtt
"""

import csv
import json
import os
from datetime import datetime

import paho.mqtt.client as mqtt

BROKER = "broker.hivemq.com"
PORTA = 1883
TOPICO_STATUS = "goodwe/fiap1cc/carregador01/status"
TOPICO_COMANDO = "goodwe/fiap1cc/carregador01/comando"

LIMITE_BATERIA = 80          # a partir daqui o script manda parar sozinho
ARQUIVO_CSV = "dados/sessoes.csv"
ARQUIVO_JSON = "dados/status.json"

# guarda o que precisa sobreviver entre uma mensagem e outra
sessao = {
    "kwh": 0.0,
    "ultima_hora": None,
    "leituras": 0,
    "leituras_solar": 0,
    "ja_avisou": False,
}


def preparar_arquivo():
    os.makedirs("dados", exist_ok=True)
    if not os.path.exists(ARQUIVO_CSV):
        with open(ARQUIVO_CSV, "w", newline="", encoding="utf-8") as f:
            escritor = csv.writer(f)
            escritor.writerow([
                "horario", "carregador", "bateria", "solar_kw",
                "fonte", "status", "kwh_acumulado"
            ])


def calcular_energia(potencia_kw, agora):
    """Energia = potencia x tempo. O tempo vem da diferenca entre duas leituras."""
    if sessao["ultima_hora"] is None:
        sessao["ultima_hora"] = agora
        return
    segundos = (agora - sessao["ultima_hora"]).total_seconds()
    sessao["kwh"] += potencia_kw * segundos / 3600
    sessao["ultima_hora"] = agora


def gravar_linha(dados, agora):
    with open(ARQUIVO_CSV, "a", newline="", encoding="utf-8") as f:
        escritor = csv.writer(f)
        escritor.writerow([
            agora.strftime("%Y-%m-%d %H:%M:%S"),
            dados["id"],
            dados["bateria"],
            dados["solar_kw"],
            dados["fonte"],
            dados["status"],
            round(sessao["kwh"], 3),
        ])


def salvar_status_atual(dados):
    """O site pode ler esse arquivo se nao quiser assinar o MQTT direto."""
    with open(ARQUIVO_JSON, "w", encoding="utf-8") as f:
        json.dump(dados, f, ensure_ascii=False, indent=2)


def ao_conectar(cliente, userdata, flags, rc):
    if rc == 0:
        print("Conectado no broker. Escutando o carregador...\n")
        cliente.subscribe(TOPICO_STATUS)
    else:
        print("Falha na conexao, codigo", rc)


def ao_receber(cliente, userdata, mensagem):
    try:
        dados = json.loads(mensagem.payload.decode())
    except json.JSONDecodeError:
        print("Mensagem fora do formato esperado, ignorando")
        return

    agora = datetime.now()
    sessao["leituras"] += 1
    if dados["fonte"] == "solar":
        sessao["leituras_solar"] += 1

    if dados["status"] == "carregando":
        calcular_energia(dados["potencia_kw"], agora)
    else:
        sessao["ultima_hora"] = agora

    gravar_linha(dados, agora)
    salvar_status_atual(dados)

    print("[{}] {} | bateria {}% | solar {} kW | {} | {} | {} kWh".format(
        agora.strftime("%H:%M:%S"),
        dados["id"],
        dados["bateria"],
        dados["solar_kw"],
        dados["fonte"],
        dados["status"],
        round(sessao["kwh"], 3),
    ))

    # automacao: acima do limite a carga para sem ninguem clicar em nada
    if dados["bateria"] >= LIMITE_BATERIA and dados["status"] == "carregando":
        cliente.publish(TOPICO_COMANDO, json.dumps({"acao": "parar", "motivo": "limite"}))
        print(">> bateria em {}%, comando de parada enviado".format(dados["bateria"]))


def resumo():
    print("\n--- resumo da sessao ---")
    print("leituras recebidas:", sessao["leituras"])
    print("energia registrada:", round(sessao["kwh"], 2), "kWh")
    if sessao["leituras"] > 0:
        percentual = sessao["leituras_solar"] / sessao["leituras"] * 100
        print("parcela com origem solar:", round(percentual), "%")
    print("historico salvo em", ARQUIVO_CSV)


preparar_arquivo()
cliente = mqtt.Client()
cliente.on_connect = ao_conectar
cliente.on_message = ao_receber
cliente.connect(BROKER, PORTA, 60)

try:
    cliente.loop_forever()
except KeyboardInterrupt:
    cliente.disconnect()
    resumo()
