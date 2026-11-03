# SinLAB1
Station MTO MQTT avec Web Embarqué pour STi2D SIN
https://arduino.esp8266.com/stable/package_esp8266com_index.json
https://dl.espressif.com/dl/package_esp32_index.json
https://espressif.github.io/arduino-esp32/package_esp32_index.json
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json

Mosquitto — broker + clients en ligne de commande

Installer

bash
# Linux / Kali (Debian/Ubuntu)
sudo apt update && sudo apt install mosquitto mosquitto-clients
# Windows : installeur depuis mosquitto.org/download
# macOS : brew install mosquitto

Lancer le broker

bash
# Linux : en service
sudo systemctl start mosquitto      # démarrer
sudo systemctl enable mosquitto     # au démarrage
sudo systemctl status mosquitto     # vérifier
# Linux/Windows : en console (verbeux, avec une config)
mosquitto -v -c /etc/mosquitto/mosquitto.conf
# Windows (service)
net start mosquitto                 # (net stop mosquitto pour arrêter)

S’abonner (laisser cette fenêtre ouverte)

bash
mosquitto_sub -h 127.0.0.1 -t "market/g1/fridge/telemetry" -v
mosquitto_sub -h 127.0.0.1 -t "#" -v        # tout écouter (joker)
# avec identifiants (Phase 2) :
mosquitto_sub -h 127.0.0.1 -u sensor-g1 -P monMotDePasse -t "market/g1/#" -v

Publier (dans une autre fenêtre)

bash
mosquitto_pub -h 127.0.0.1 -t "market/g1/fridge/cmd" -m "{\"defrost\":true}"
mosquitto_pub -h 127.0.0.1 -t "market/g1/fridge/telemetry" -m "{\"temp\":4,\"hum\":55}"
# en TLS (Phase 2, port 8883) :
mosquitto_pub -h 127.0.0.1 -p 8883 --cafile ca.crt -u sensor-g1 -P monMotDePasse \
  -t "market/g1/fridge/telemetry" -m "{\"temp\":4}"
paho-mqtt — bibliothèque cliente Python (il faut un broker qui tourne à côté)

Installer

bash
pip install paho-mqtt

S’abonner (programme Python — enregistre-le puis lance python abonne.py)

python
import paho.mqtt.client as mqtt

def on_message(client, userdata, msg):
    print(msg.topic, "=", msg.payload.decode())

c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
c.on_message = on_message
c.connect("127.0.0.1", 1883, 60)   # ← se connecte au broker Mosquitto
c.subscribe("market/g1/fridge/telemetry")   # ou "#"
c.loop_forever()

Publier (programme Python — python publie.py)

python
import paho.mqtt.publish as publish

publish.single(
    "market/g1/fridge/cmd",
    '{"defrost":true}',
    hostname="127.0.0.1", port=1883)
python
# équivalent avec un client complet (utile si auth Phase 2) :
import paho.mqtt.client as mqtt
c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
c.username_pw_set("sensor-g1", "monMotDePasse")   # Phase 2
c.connect("127.0.0.1", 1883, 60)
c.publish("market/g1/fridge/telemetry", '{"temp":4}')
c.disconnect()
