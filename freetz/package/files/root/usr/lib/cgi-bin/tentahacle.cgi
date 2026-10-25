#!/bin/sh
# SPDX-License-Identifier: MIT OR Apache-2.0

. /usr/lib/libmodcgi.sh

sec_begin "$(lang de:"Dienst" en:"Service")"
cgi_print_radiogroup_service_starttype "enabled" "$TENTAHACLE_ENABLED" "" "" 0
sec_end

sec_begin "MQTT"
cgi_print_textline_p "mqtt_host" "$TENTAHACLE_MQTT_HOST" 40/255 "$(lang de:"Broker-Host oder IP-Adresse" en:"Broker host or IP address"): "
cgi_print_textline_p "mqtt_port" "$TENTAHACLE_MQTT_PORT" 6/5 "Port: "
cgi_print_textline_p "mqtt_username" "$TENTAHACLE_MQTT_USERNAME" 40/255 "$(lang de:"MQTT-Benutzername" en:"MQTT username"): "
cgi_print_password_p "mqtt_password" "$TENTAHACLE_MQTT_PASSWORD" 40/255 "$(lang de:"MQTT-Passwort" en:"MQTT password"): "
cgi_print_textline_p "bridge_id" "$TENTAHACLE_BRIDGE_ID" 32/32 "Bridge ID: "
cgi_print_checkbox "debug" "$TENTAHACLE_DEBUG" "$(lang de:"Debug-Protokoll" en:"Debug logging")"
cat <<EOF
<p>$(lang \
de:"Verwende die Adresse des Brokers, den auch Home Assistant nutzt. Bei der offiziellen Mosquitto-App kann daf&uuml;r ein eigener Home-Assistant-Benutzer verwendet werden. Die Bridge ID muss aus 3&ndash;32 Kleinbuchstaben, Zahlen, <code>_</code> oder <code>-</code> bestehen und sollte nach dem Anlegen virtueller Ger&auml;te nicht mehr ge&auml;ndert werden." \
en:"Use the address of the broker already used by Home Assistant. With the official Mosquitto app, a dedicated Home Assistant user can be used. The bridge ID must contain 3&ndash;32 lowercase letters, numbers, <code>_</code> or <code>-</code> and should not be changed after creating virtual devices.")</p>
EOF
sec_end

sec_begin "$(lang de:"Hinweis" en:"Note")"
cat <<EOF
<p>$(lang \
de:"FRITZ!OS bleibt der Layout-Editor f&uuml;r das FRITZ!Smart Control 440. Virtuelle Ger&auml;te werden in Home Assistant angelegt und danach in FRITZ!OS einem Anzeigeplatz zugewiesen. F&uuml;r die laufende Nutzung sind weder SSH noch manuelle Konfigurationsdateien erforderlich." \
en:"FRITZ!OS remains the layout editor for the FRITZ!Smart Control 440. Create virtual devices in Home Assistant and then assign them to a display position in FRITZ!OS. Normal operation requires neither SSH nor manual configuration files.")</p>
EOF
sec_end
