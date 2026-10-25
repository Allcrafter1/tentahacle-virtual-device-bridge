# Installation auf einer separaten FRITZ!Box 7530

[Deutsch](installation.de.md) | [English](installation.md)

Diese Anleitung führt von einer unbenutzten kompatiblen FRITZ!Box bis zum
ersten funktionierenden virtuellen Gerät auf dem FRITZ!Smart Control 440:

1. Bridge-Firmware lokal bauen und installieren;
2. Netzwerk und MQTT über die Weboberflächen konfigurieren;
3. die Home-Assistant-Integration über HACS installieren;
4. ein virtuelles Gerät erzeugen und in FRITZ!OS zuweisen.

Nach der einmaligen Firmware-Vorbereitung sind für die normale Einrichtung und
Nutzung weder SSH noch Terminalbefehle oder manuelle Konfigurationsdateien
erforderlich.

> [!WARNING]
> Version 0.2.0-beta.1 ist ein klarer Neustart gegenüber den früheren
> 0.1.x-Testständen. Sie verwendet bewusst neue Kennungen für Home Assistant,
> MQTT, Freetz, Dateipfade und virtuelle Geräte. Eine automatische Migration
> gibt es nicht: Entferne die alte Test-Integration und das alte Bridge-Paket,
> installiere diese Beta frisch und lege virtuelle Geräte samt Zuweisungen neu
> an. Sichere die Box vorher, falls du den bisherigen Laborstand behalten
> möchtest.

Wenn dich gerade das Modifizieren der FRITZ!Box von der Nutzung abhält, eröffne
ein [GitHub-Issue](https://github.com/Allcrafter1/tentahacle-virtual-device-bridge/issues).
Der Maintainer ist grundsätzlich bereit, die Einrichtung einer kompatiblen Box
mit Nutzern gemeinsam durchzugehen und wiederkehrende Schwierigkeiten in einen
einfacheren Installationsweg zu überführen. Hänge dort keine Passwörter,
Konfigurationsexporte, Seriennummern oder andere private Gerätedaten an.

## Unterstützter Ausgangsstand

Die erste öffentliche Version unterstützt bewusst nur:

- klassische FRITZ!Box 7530, Hardware-Revision 236;
- deutsche FRITZ!OS-Version 8.25;
- den unter [Kompatibilität](compatibility.md) genannten `aha`-Fingerabdruck.

Verwende das Image nicht auf einem anderen Modell oder Firmwarestand. Nutze
eine separate Bridge-Box, nicht die FRITZ!Box, die deinen Internetzugang
bereitstellt. Exportiere vor der Installation ihre FRITZ!Box-Konfiguration und
halte das offizielle AVM-Wiederherstellungsverfahren bereit. Deaktiviere nach
der Installation automatische FRITZ!OS-Updates auf der Bridge.

Du brauchst:

- eine x86-64-Linux-Umgebung mit Git und den normalen Freetz-NG-Abhängigkeiten;
  unterstützt werden natives Linux sowie Ubuntu unter WSL2 auf Windows;
- die separate FRITZ!Box 7530;
- einen MQTT-Broker, den Home Assistant und die 7530 erreichen können;
- Home Assistant mit eingerichteter MQTT-Integration;
- eine direkte Ethernet-Verbindung für die Erstinstallation.

Die erste Installation ist am einfachsten, wenn nur Computer und 7530 direkt
per Ethernet verbunden sind. IP-Client- und optionaler Mesh-Betrieb werden
danach in der normalen FRITZ!OS-Oberfläche konfiguriert. Die produktive
FRITZ!Box bleibt unverändert.

## Installationsumgebung auswählen

### Natives x86-64-Linux

Ubuntu, Debian und andere von Freetz-NG unterstützte Build-Systeme können das
mitgelieferte Skript direkt ausführen. Für den Build braucht der Computer einen
Internetzugang. Für die Erstinstallation wird sein Ethernet-Anschluss direkt
mit der separaten 7530 verbunden.

### Windows mit WSL2 und Ubuntu – praktisch getestet

Die ursprüngliche Labor-Bridge wurde erfolgreich unter Windows mit x86-64-WSL2
und Ubuntu vorbereitet. Dieser Weg wird deshalb von diesem Projekt unterstützt,
auch wenn Freetz-NG WSL-Umgebungen allgemein als möglicherweise problematisch
einstuft.

Öffne PowerShell als Administrator und installiere beziehungsweise aktualisiere
WSL2:

```powershell
wsl --install -d Ubuntu
wsl --update
```

Starte Windows neu, falls es verlangt wird. Öffne danach Ubuntu. Lege das
Repository im Linux-Home und nicht unter `/mnt/c` ab:

```sh
sudo apt update
sudo apt install --yes git
mkdir -p ~/src
cd ~/src
git clone https://github.com/Allcrafter1/tentahacle-virtual-device-bridge.git
cd tentahacle-virtual-device-bridge
uname -m
```

`uname -m` muss `x86_64` ausgeben. Verbinde die separate 7530 direkt per
Ethernet. Falls Windows die Netzwerkadresse während des Neustarts der Box nicht
hält, trage vorübergehend `192.168.178.2` mit der Subnetzmaske
`255.255.255.0` am Windows-Ethernetadapter ein. VPN- oder Firewall-Software,
die FTP-Verbindungen abfängt, kann den nur kurz erreichbaren EVA-Bootloader
blockieren. Pausiere sie bei Bedarf für diese isolierte Direktverbindung.

Danach gilt derselbe Befehl wie unter nativem Linux:

```sh
./tools/build-firmware.sh --install-prerequisites --flash
```

### Installation mit Codex unter WSL2 – praktisch getestet

[Codex kann direkt in WSL2 ausgeführt
werden](https://learn.chatgpt.com/docs/windows/wsl). Öffne Codex im geklonten
Repository und kopiere den folgenden Prompt. Ersetze die Werte in eckigen
Klammern, soweit nötig.

```text
Richte TentAHAcle - Virtual Device Bridge aus diesem Repository auf meiner separaten
Labor-FRITZ!Box 7530 ein. Arbeite selbstständig und führe die Einrichtung so
weit durch, wie es die lokale Umgebung erlaubt. Untersuche zuerst die Anleitung,
den Repository-Stand und die aktuelle Umgebung. Prüfe, dass dies x86-64-Linux
oder WSL2 ist, installiere nur die notwendigen Build-Abhängigkeiten und verwende
das gepinnte Buildskript des Repositorys.

Zielhardware: klassische FRITZ!Box 7530, HWRevision 236.
Erforderliche Zielfirmware: deutsches FRITZ!OS 8.25.
Bootloader-Adresse: 192.168.178.1.

Dies ist eine separate Bridge-Box und nicht mein produktiver Internetrouter.
Verändere keine andere FRITZ!Box. Lade kein vorgefertigtes AVM- oder
modifiziertes Firmware-Image herunter, veröffentliche keines und committe
keines. Baue das Image wie dokumentiert lokal. Prüfe vor dem Flashen Modell und
Firmware mit mir, stelle sicher, dass ich die originale FRITZ!Box-Konfiguration
exportiert habe, und zeige mir das exakte lokal erzeugte Image. Starte danach
das interaktive Erstinstallationsverfahren des Repositorys. Beziehe mich nur
bei manuellen Handlungen ein, die du nicht selbst ausführen kannst: Ethernet
anschließen, Strom trennen oder wieder verbinden und den tatsächlichen
Flashvorgang bestätigen.

Führe mich nach dem Start der Box durch die normalen FRITZ!OS- und
Freetz-Weboberflächen: IP-Client einrichten, automatische FRITZ!OS-Updates
deaktivieren, Freetz absichern und den MQTT-Broker konfigurieren. Mein
Broker-Host ist [HOME_ASSISTANT_IP], Port 1883; den dafür vorgesehenen
MQTT-Benutzernamen und das Passwort gebe ich selbst ein. Führe mich danach
durch das Hinzufügen des Repositorys zu HACS, die Installation der
Home-Assistant-Integration, das Erstellen eines virtuellen Testgeräts und seine
Zuweisung in FRITZ!OS. Prüfe jeden abgeschlossenen Schritt und dokumentiere
jede Abweichung vom angegebenen Kompatibilitätsstand.
```

Codex kann Umgebung und Hardwarestand prüfen, bauen und das Installationsskript
starten. Stromversorgung und Ethernet müssen physisch bedient werden; die
endgültige Flashentscheidung bleibt beim Nutzer. Falls die Box nicht HW236 mit
FRITZ!OS 8.25 entspricht, muss vor dem Flashen angehalten werden.

## Firmware lokal bauen und erstmals installieren

Führe im Repository unter nativem Linux oder WSL2 aus:

```sh
./tools/build-firmware.sh --flash
```

Falls die Build-Pakete noch fehlen:

```sh
./tools/build-firmware.sh --install-prerequisites --flash
```

Freetz-NG zeigt benötigte Pakete an und verlangt `sudo` nur, wenn wirklich
Pakete installiert werden müssen. Das Skript:

- lädt den festgelegten Freetz-NG-Commit unter `.build`;
- fügt das TentAHAcle-Bridge-Quellpaket hinzu;
- wählt das geprüfte 7530-/8.25-Profil;
- baut die Firmware lokal;
- startet danach interaktiv `push_firmware`.

Das Flashen beginnt nur mit `--flash` und nach der interaktiven Bestätigung.
Der EVA-Bootloader nutzt standardmäßig `192.168.178.1`. Wenn der Computer beim
Neustart der Box keine Adresse in diesem Netz behält, verwende vorübergehend
`192.168.178.2/24`. Eine abweichende Bootloader-Adresse kann mit
`--box-ip ADRESSE` angegeben werden.

Ohne `--flash` wird ausschließlich gebaut:

```sh
./tools/build-firmware.sh --install-prerequisites
```

Das Ergebnis liegt unter `.build/freetz-ng/images`. Veröffentliche oder
verteile dieses Image nicht. Weder Repository noch GitHub-Release enthalten
eine AVM- oder modifizierte Firmware.

Für erfahrene Nutzer stehen zusätzlich zur Verfügung:

```sh
./tools/build-firmware.sh --menuconfig
./tools/install-freetz-package.sh /pfad/zu/freetz-ng
```

Im zweiten Fall muss in Freetz-NG unter **Packages → T → TentAHAcle - Virtual
Device Bridge** das Paket aktiviert werden.

## MQTT vorbereiten

Die Bridge betreibt keinen eigenen Broker. Home Assistant und die 7530 sind
zwei getrennte MQTT-Clients und benötigen jeweils eine funktionierende
Verbindung zum vorhandenen Broker.

Mit der offiziellen Home-Assistant-App **Mosquitto broker** ist ein eigener,
nur für diese Bridge verwendeter Login der einfachste Standardweg:

1. Öffne **Einstellungen → Apps → Mosquitto broker → Konfiguration**.
2. Ergänze unter `logins` einen eigenen Zugang:

   ```yaml
   logins:
     - username: tentahacle
       password: ein-langes-eigenes-passwort
   ```

3. Speichere die Konfiguration und starte die Mosquitto-App neu.
4. Verwende später in Freetz genau diesen Benutzernamen und dieses Passwort.

Home Assistant verwendet für seine eigene MQTT-Integration weiterhin seinen
bereits gespeicherten Zugang. Verwende auf der 7530 die im LAN erreichbare
IP-Adresse oder den Hostnamen der Home-Assistant-Maschine. Der interne Name
`core-mosquitto` ist von der FRITZ!Box normalerweise nicht erreichbar. Der
unverschlüsselte Standardport im lokalen Netz ist `1883`.

Die Mosquitto-App unterstützt alternativ einen eigenen Home-Assistant-Benutzer.
Der lokale `logins`-Eintrag wird hier empfohlen, weil er sichtbar nur für MQTT
bestimmt ist. Fortgeschrittene Nutzer können den Account zusätzlich per ACL
auf `tentahacle/<bridge-id>/#` beschränken. Die offizielle Mosquitto-App
erlaubt keine anonyme Anmeldung.

## Einrichtung nach dem Flashen

1. Öffne die normale FRITZ!OS-Oberfläche der Bridge. Richte sie als IP-Client
   und bei Bedarf als Mesh Repeater ein. Vergib eine feste Adresse oder eine
   DHCP-Reservierung. Wähle unter **System → Update → Auto-Update** die Option
   **Über neue FRITZ!OS-Versionen informieren**. Falls die Bridge im Mesh
   bleibt, deaktiviere außerdem unter **Heimnetz → Mesh → Mesh Einstellungen**
   die Übernahme der Einstellungen des Mesh Masters. Aktualisiere diese Box
   auch nicht manuell aus der Mesh-Übersicht, solange der neue Stand hier nicht
   freigegeben ist.
2. Öffne die Freetz-Weboberfläche unter `http://<bridge-ip>:81` und sichere den
   Administrationszugang ab.
3. Öffne **Pakete → TentAHAcle - Virtual Device Bridge**.
4. Trage Broker-Adresse, Port, den vorbereiteten MQTT-Benutzer, sein Passwort
   und eine eindeutige Bridge-ID ein. Die Bridge-ID darf 3-32 Kleinbuchstaben,
   Zahlen, `_` oder `-` enthalten und sollte nach dem Anlegen von Geräten nicht
   geändert werden.
5. Aktiviere den Dienst und übernimm die Konfiguration. Freetz speichert die
   Werte dauerhaft und erzeugt eine nur für root lesbare Laufzeitdatei. SSH und
   manuelle Dateien sind dafür nicht erforderlich.

Der MQTT-Broker sollte danach die beibehaltenen Meldungen `bridge/info` und
`bridge/availability` enthalten. `online` zusammen mit `ready: false` bedeutet,
dass MQTT funktioniert, der lokale Provider aber noch nicht vollständig bereit
ist.

## Home Assistant über HACS installieren

1. Öffne in HACS **Benutzerdefinierte Repositories**.
2. Füge
   `https://github.com/Allcrafter1/tentahacle-virtual-device-bridge` mit der Kategorie
   **Integration** hinzu.
3. Installiere **TentAHAcle - Virtual Device Bridge**.
4. Starte Home Assistant neu.

Die beibehaltene MQTT-Ankündigung sollte die Bridge automatisch entdecken.
Alternativ kann die Integration manuell hinzugefügt werden; verwende dann
dieselbe Bridge-ID und den Topic-Stamm `tentahacle`.

## Erstes virtuelles Gerät anlegen

1. Öffne **Einstellungen → Geräte & Dienste → TentAHAcle - Virtual Device Bridge**.
2. Verwende oben rechts **Virtuelles FRITZ!-Gerät hinzufügen**.
3. Wähle eine Home-Assistant-Entität. Das engste passende Profil wird
   vorgeschlagen; optionale Fähigkeiten können reduziert werden.
4. Vergib den FRITZ!-Gerätenamen und bestätige.
5. Folge auf der Abschlussseite dem Link **FRITZ!OS-Oberfläche öffnen**.
6. Weise das virtuelle Gerät dort dem gewünschten Anzeigeplatz des 440 zu und
   passe bei Bedarf den kurzen Displaynamen an.

Die Bridge erlaubt eine dauerhafte virtuelle FRITZ!-Identität pro
Home-Assistant-Entität. Dasselbe virtuelle Gerät kann auf mehreren 440 liegen.
Beim Austausch einer HA-Entität kann die Zuordnung neu gebunden werden, ohne
FRITZ!-Identität und vorhandene 440-Plätze zu verlieren. Ein Wechsel des
Gerätetyps erzeugt dagegen ein neues virtuelles Gerät.

Wenn ein Mapping in Home Assistant gelöscht wird, entfernt die Integration
auch das zugehörige virtuelle Gerät aus FRITZ!OS und aus dem Bridge-Register.

## FRITZ!Smart Control 440 direkt mit der Bridge verbinden

Für kurze Reaktionszeiten muss das 440 direkt an der Bridge-7530 angemeldet
sein. Die 7530 darf dabei als IP-Client im LAN und auch als Mesh Repeater
bleiben; der Tastendruck erreicht dann trotzdem zuerst ihr lokales `aha`. Für
die Bridge-Funktion selbst ist Mesh nicht erforderlich. Wer die stärkste
Trennung vom produktiven Router möchte, entfernt die 7530 nach ihrer
IP-Client-Einrichtung vollständig aus dem Mesh.

Führe den Umzug erst aus, nachdem Paketdienst, MQTT und mindestens ein
virtuelles Gerät funktionieren:

1. Sichere 6690 und 7530 wie im nächsten Abschnitt beschrieben.
2. Öffne auf der 7530 **Smart Home → Geräte und Gruppen → Gerät anmelden**.
3. Setze das 440 über sein Menü auf Werkseinstellungen und starte anschließend
   dort **Anmeldung starten**. Die genauen Symbole können sich mit der
   Gerätefirmware ändern.
4. Weise das virtuelle Gerät in der Oberfläche der 7530 erneut einem
   Anzeigeplatz zu und teste Befehl und bestätigten Zustand in beide
   Richtungen.
5. Lösche den alten, nun nicht mehr verbundenen 440-Eintrag auf der 6690 erst
   nach dem erfolgreichen Test.

Die auf der 6690 gespeicherten Anzeigeplätze werden nicht automatisch auf die
7530 übertragen. Ein Export der 6690 ist deshalb eine Rückfallmöglichkeit,
aber kein Migrationswerkzeug für das 440-Layout.

FRITZ beschreibt das [Zurücksetzen des 440](https://fritz.com/apps/knowledge-base/FRITZ-Box-7590/3722_Werkseinstellungen-des-FRITZ-Tasters-laden/)
und die anschließende Anmeldung in der eigenen Wissensdatenbank.

## Thermostat-Zeitplan

Bei einem Zigbee2MQTT-Thermostat erkennt die Integration den geprüften
5+2-Zeitplan automatisch, wenn passend benannte `select.*_week`,
`text.*_workdays_schedule` und `text.*_holidays_schedule` vorhanden sind. Home
Assistant bleibt die führende Konfiguration. Die Bridge spiegelt die aktuelle
und nächste Änderung nur für die native Anzeige auf dem 440.

Bearbeite den Zeitplan des virtuellen Thermostats nicht in FRITZ!OS oder der
FRITZ!App. Die Bridge verwirft diese Änderung und stellt die Schattenkopie aus
Home Assistant wieder her. Thermostate ohne passenden Zeitplanadapter behalten
Solltemperatur, Modus sowie Wärme- und Kalt-Timer; nur die Anzeige der nächsten
Änderung fehlt.

## Bridge sichern

Erstelle vor jedem Firmwarewechsel und nach einer größeren Gerätekonfiguration
zwei Sicherungen:

1. Unter **FRITZ!OS → System → Sicherung → Sichern** einen kennwortgeschützten
   FRITZ!Box-Export. Er enthält die AVM-Konfiguration einschließlich der
   Smart-Home-Einstellungen.
2. Unter **Freetz → System → Sichern & Wiederherstellen** ein verschlüsseltes
   Freetz-Backup. Dieses enthält auch die persistente Geräte-Registry und die
   MQTT-Konfiguration von TentAHAcle - Virtual Device Bridge. Behandle die Datei wegen der
   enthaltenen Zugangsdaten vertraulich und bewahre ihr Kennwort getrennt auf.

Nach dem Einspielen einer FRITZ!Box-Sicherung können DECT-Geräte trotz
erhaltener Konfiguration eine erneute Funkanmeldung benötigen. Melde sie dann
über den erweiterten Anmeldemodus beziehungsweise die Gerätetaste oder das
Gerätemenü erneut an, statt den erhaltenen Eintrag zu löschen und von vorn
anzulegen. Das Freetz-Backup gilt nur für die dafür vorgesehene Bridge-Box.
Das entspricht dem von FRITZ dokumentierten
[Wiederherstellungsablauf für Smart-Home- und DECT-Geräte](https://fritz.com/apps/knowledge-base/FRITZ-Box-7530/4_Einstellungen-der-FRITZ-Box-sichern-und-wiederherstellen/).

## Aktualisieren

Automatische FRITZ!OS-Updates müssen auf der Bridge ausgeschaltet bleiben. Ein
neuer Firmwarestand kann die private `aha`-Schnittstelle verändern und wird
erst nach einer erneuten Prüfung freigegeben.

Eine bereits mit Freetz laufende 7530 kann normalerweise aktualisiert werden,
indem ein neues Image ohne `--flash` lokal gebaut und anschließend über die
Freetz-Weboberfläche eingespielt wird. Erstelle vorher beide Sicherungen. Das
persistente Register unter `/tmp/flash/tentahacle/registry.json` hält
Geräteidentitäten über kompatible Neustarts und Updates hinweg und ist im
Freetz-Backup enthalten.

Die Home-Assistant-Integration wird über HACS aktualisiert. Beide Seiten
wiederholen ihre gewünschte Konfiguration nach einer MQTT-Wiederverbindung;
die Startreihenfolge ist daher egal.

## Diagnose

Prüfe zuerst den Dienststatus in Freetz. Die wichtigsten Logs auf der Bridge
sind:

```text
/var/log/tentahacle-aha.log
/var/log/tentahacle-mqtt.log
```

Ein Fingerabdruckfehler bedeutet, dass der installierte FRITZ!OS-Stand bewusst
nicht unterstützt wird. Zugangsdaten werden von Freetz in eine Laufzeitdatei
mit Modus `0600` geschrieben und sollten nicht in Fehlerberichte kopiert
werden.

## Rückkehr zum unveränderten Betrieb

Das Deaktivieren des Dienstes in Freetz beendet nur die MQTT-Bridge und den mit
dem Provider gestarteten `aha`-Prozess. Danach wird das originale `aha` ohne
`LD_PRELOAD` gestartet. Virtuelle Geräte können in FRITZ!OS als nicht verfügbar
stehen bleiben und nach Prüfung vorhandener 440-Zuweisungen entfernt werden.

Ein unverändertes AVM-Image beziehungsweise eine normale Freetz-Firmware ohne
das Paket entfernt TentAHAcle - Virtual Device Bridge vollständig. Für ungewöhnliche
Erstinstallations- und Wiederherstellungssituationen bleibt die
[Freetz-NG-Installationsdokumentation](https://freetz-ng.github.io/freetz-ng/INSTALL/)
maßgeblich.
