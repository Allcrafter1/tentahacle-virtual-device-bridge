# TentAHAcle - Virtual Device Bridge

Virtuelle Home-Assistant-Geräte für FRITZ! Smart Home und FRITZ!Smart Control 440.

[Deutsch](README.de.md) | [English](README.md)

Mit TentAHAcle - Virtual Device Bridge kannst du Geräte aus Home Assistant
über ein **FRITZ!Smart Control 440** bedienen, obwohl FRITZ!OS diese Geräte
normalerweise nicht dem 440 zuordnen kann.

Für jede ausgewählte Home-Assistant-Entität erzeugt die Bridge ein dauerhaftes
virtuelles FRITZ!-Smart-Home-Gerät. Dieses Gerät weist du wie ein echtes
FRITZ!-Gerät über die normale FRITZ!OS-Oberfläche dem 440 zu. Befehle und
bestätigte Zustände werden anschließend in beide Richtungen über lokales MQTT
übertragen.

So kann beispielsweise eine Zigbee-, Matter-, Hue- oder WLAN-Lampe, die bereits
in Home Assistant funktioniert, auf dem 440 als FRITZ!-Lampe erscheinen. Je
nach gewähltem Profil kannst du sie schalten, dimmen und ihre Farbtemperatur
ändern.
Bei einer kompatiblen Farblampe wird außerdem die Farbpalette des 440 auf die
Home-Assistant-Lampe übertragen und die bestätigte Farbe zurückgemeldet.

> [!IMPORTANT]
> Dies ist unabhängige, experimentelle Community-Software. Der aktuelle Kandidat
> unterstützt eine genau festgelegte Kombination aus Bridge-Hardware und
> Firmware. Verwende dafür eine separate FRITZ!Box und nicht deinen zentralen
> Internetrouter.
>
> TentAHAcle steht in keiner Verbindung zur FRITZ! GmbH und wird von ihr weder
> empfohlen, gesponsert noch unterstützt. Produktnamen dienen ausschließlich
> der Beschreibung der Kompatibilität. Wende dich für dieses Projekt nicht an
> den FRITZ!-Support. Siehe [Markenhinweis](TRADEMARKS.md).

## Das brauchst du

- ein FRITZ!Smart Control 440;
- eine separate klassische **FRITZ!Box 7530 / HW236 mit FRITZ!OS 8.25**;
- Home Assistant mit erreichbarem MQTT-Broker und eingerichteter
  MQTT-Integration;
- Ethernet für die einmalige Installation der Bridge;
- eine der getesteten Build-Umgebungen:
  - x86-64-Linux oder
  - Windows mit x86-64-WSL2 und Ubuntu.

Der Weg Windows -> WSL2 wurde für die ursprüngliche Laborinstallation
erfolgreich verwendet und wird unterstützt. Codex kann einen Großteil der
Einrichtung direkt in WSL2 prüfen und ausführen.

## Hier anfangen

1. **Bridge vorbereiten:** Das Paket unter Linux oder WSL2 lokal bauen und auf
   der separaten 7530 installieren.
2. **Bridge verbinden:** Die 7530 als IP-Client einrichten und anschließend in
   der Freetz-Oberfläche den MQTT-Broker samt eigenem Login eintragen.
3. **Integration installieren:** Dieses Repository in HACS als Integration
   hinzufügen, **TentAHAcle - Virtual Device Bridge** installieren und Home
   Assistant neu starten.
4. **Geräte anlegen:** Eine Home-Assistant-Entität auswählen, das virtuelle
   FRITZ!-Gerät erzeugen und dem Link zu FRITZ!OS folgen, um es dem 440
   zuzuweisen.

**[Vollständige Installationsanleitung öffnen](docs/installation.de.md)**

Die Anleitung enthält den getesteten Windows-/WSL2-Weg, einen kopierbaren
Prompt für die Installation mit Codex, MQTT- und HACS-Einrichtung sowie Update,
Diagnose und Wiederherstellung.

Wenn dir das Modifizieren der FRITZ!Box zu kompliziert erscheint, eröffne ein
[GitHub-Issue](https://github.com/Allcrafter1/tentahacle-virtual-device-bridge/issues).
Der Maintainer ist grundsätzlich bereit, die Einrichtung einer kompatiblen Box
mit Nutzern gemeinsam durchzugehen und den Installationsweg anhand dieser
Erfahrung weiter zu verbessern. Veröffentliche im Issue niemals Passwörter,
Konfigurationsexporte, Seriennummern oder andere private Gerätedaten.

## Unterstützte virtuelle Geräte

| Home-Assistant-Quelle | Virtuelles FRITZ!-Gerät | Verfügbare Funktionen |
|---|---|---|
| `switch`, `input_boolean` | Schalter/Steckdose | Ein/Aus |
| `light` | dimmbare Lampe oder Farblampe | Ein/Aus, Helligkeit, Farbtemperatur soweit unterstützt sowie Hue/Saturation für HA-Lampen mit `hs`, `xy`, `rgb`, `rgbw` oder `rgbww` |
| `cover` | Rollladen | Öffnen/Schließen; Zwischenposition nur mit `SET_POSITION`, Stoppen nur mit `STOP` |
| `climate` | Heizkörperthermostat | Heizen/Aus, Sollwert 8–28 °C, MAX, Isttemperatur, Wärme-/Kalt-Timer, Celsius-/Fahrenheit-Umrechnung und optional Anzeige der nächsten Zeitplanänderung |

FRITZ!OS bleibt der Layout-Editor für das 440. Home Assistant verwaltet die
Verknüpfung zwischen virtuellem FRITZ!-Gerät und echter Entität. Ein virtuelles
Gerät kann auf mehreren Bedieneinheiten liegen. Beim Austausch einer
Home-Assistant-Entität können FRITZ!-Identität und vorhandene 440-Zuweisungen
erhalten bleiben.

Eine Bridge unterstützt derzeit höchstens **64 virtuelle Geräte**. Das ist
eine konservative Grenze unserer statisch dimensionierten Registry und der
Home-Assistant-Oberfläche, keine nachgewiesene FRITZ!OS- oder `aha`-Grenze.
Dasselbe virtuelle Gerät darf auf mehreren 440 liegen und zählt trotzdem nur
einmal. Anlegen, Umbenennen, Neubinden und Löschen erfolgen in Home Assistant;
beim Löschen eines Mappings wird auch das native Gerät entfernt. Das virtuelle
Elterngerät lässt sich in FRITZ!OS absichtlich nicht unabhängig löschen.
FRITZ!OS bleibt für das Controller-Layout sowie native Vorlagen, Szenarien und
Routinen zuständig; die Bridge baut diese Funktionen nicht in Home Assistant
nach.

## Aktuelle Kompatibilität und Grenzen

Der vorherige Laborstand 0.1.5 wurde mit diesem Aufbau geprüft:

- FRITZ!Box 7530 classic / HW236, FRITZ!OS 8.25;
- FRITZ!Box 6690 Cable, FRITZ!OS 8.25, als Mesh Master;
- FRITZ!Smart Control 440, Firmware 05.45;
- Home Assistant mit MQTT.

Die Bridge prüft den exakten Fingerabdruck des internen `aha`-Programms. Bei
einem unbekannten Stand startet der virtuelle Provider nicht. Automatische
FRITZ!OS-Updates müssen auf der Bridge deaktiviert bleiben, bis eine neue
Version untersucht und freigegeben wurde.

Der getestete Weg über einen Mesh Master funktioniert, verursachte im Labor
aber etwa 6-7 Sekunden Befehlsverzögerung. Für die vorgesehene Topologie wird
das 440 direkt mit der Bridge-Box verbunden. Schalten und Dimmen wurden dort
inzwischen in beide Richtungen geprüft. Der aktuelle Entwicklungskandidat
ergänzt Vollfarbe und Isttemperatur, fähigkeitsabhängige Cover-Steuerung,
Thermostat-MAX, die Gerätebegrenzung sowie weitere Wiederherstellungs- und
Lebenszykluskorrekturen. Diese Pfade sind automatisiert abgedeckt, aber noch
nicht alle mit den unten genannten gezielten Hardwaretests releasequalifiziert.
Die exakten Daten stehen unter
[Kompatibilität](docs/compatibility.md).

Bekannte Grenzen des aktuellen Kandidaten:

- Die Farbtemperaturseite kennt genau neun native Kelvinstufen. Kurze
  Tastendrücke laufen durch diese Stufen; FRITZ!OS 8.25 springt dabei am Ende
  jeweils zum anderen Ende der Skala. Die Bridge behält dieses native
  Verhalten bei, weil der absolute Endpunktbefehl nicht zuverlässig erkennen
  lässt, ob er durch diesen Sprung entstanden ist. Lampen ohne echten
  `color_temp`-Modus können Kelvin in Home Assistant nur über Farbe emulieren,
  und ein Lampenbereich ohne eine der neun Stufen ist nicht vollständig
  abbildbar.
- Binäre Rollläden melden bestätigte Endpositionen, aber während der Fahrt
  keine erfundene Position. Nicht unterstützte Stopp- oder
  Zwischenpositionsbefehle werden abgewiesen.
- FRITZ!OS kann das Elterngerät des virtuellen Rollladens kosmetisch als
  Licht/LED anzeigen und wegen der fehlenden echten Kalibrierung vor nicht
  gesetzten Endlagen warnen.
- Ein Thermostatwert über 28 °C ist nur darstellbar, wenn er genau dem MAX-Wert
  der Quellentität entspricht. Andere FRITZ-Thermostateinstellungen wie
  Sperren, Offset, Sensorwahl, adaptive Heizung oder Urlaubsmodus werden nicht
  übertragen.
- Eine bereits geöffnete 440-Seite aktualisiert sich mitunter nicht sofort.
  Nach erneutem Öffnen muss der bestätigte aktuelle Zustand erscheinen; dieses
  Verhalten wurde auch mit einem originalen Thermostat reproduziert.
- Virtuelle Geräte lassen sich in nativen Vorlagen und Szenarien auswählen und
  speichern. Die Laufzeitprüfung von Vorlagen, Szenarien und Routinen gehört
  aber noch zur Release-Checkliste.

### Projektstand und Mitmachen

Das Projekt ist funktionierende experimentelle Software und noch kein fertiges
Produkt. Das exakte aus Commit `ae76886` gebaute Image `0.3.0-rc6` ist auf der
beschriebenen Labor-7530 zusammen mit der passenden Home-Assistant-Komponente
installiert. Alle neun aktuellen Geräteidentitäten blieben erhalten, der
alternative Boot-Slot 0 startet sauber und die 6690 meldet alle virtuellen
Geräte wieder als anwesend. RC6 unterscheidet einen antwortenden Provider mit
vorübergehend getrennter nativer Verbindung von einem hängenden Kontrollkanal.
Ein wiederholtes Update eines FRITZ!Smart Control 440 von 05.38 auf 05.45 lief
ohne AHA-Neustart durch; echte Fehlerpfade behalten ihre begrenzte Recovery.
Die genauen Nachweise stehen in der Release-Checkliste. Besonders hilfreich
bleiben längere Praxistests und Prüfungen auf zusätzlicher unterstützter
Hardware.
Nachvollziehbare Ergebnisse bitte über
[GitHub Issues](https://github.com/Allcrafter1/tentahacle-virtual-device-bridge/issues) melden.

Dieses Repository verteilt keine AVM-Firmware, keine AVM-Programme und keine
modifizierten Firmware-Images. Jeder Nutzer baut sein Image lokal selbst. Lies
vorher die [Lizenz- und Distributionsgrenzen](docs/licensing.md).

## Technische Dokumentation

- [Installation, Update und Wiederherstellung](docs/installation.de.md)
- [Architektur und Bedienablauf](docs/architecture.md)
- [Kompatibilitätsregeln](docs/compatibility.md)
- [Begrenzte Release-Prüfliste](docs/release-validation.md)
- [MQTT-Protokoll Version 1](docs/mqtt-protocol.md)
- [Produktentscheidungen](docs/decisions.md)
- [Lizenz- und Distributionsgrenzen](docs/licensing.md)
- [Marken- und Unabhängigkeitshinweis](TRADEMARKS.md)

## Hinweis zur Entwicklung

Wesentliche Teile der Interoperabilitätsforschung, Implementierung und
Dokumentation entstanden mit Unterstützung generativer KI. Architektur,
Quellcode und Verhalten wurden anschließend am dokumentierten Laboraufbau
geprüft. Beiträge und unabhängige Verifikation sind willkommen.

## Lizenz

Der eigene Projektcode steht wahlweise unter der freizügigen
[Apache License 2.0](LICENSE) oder der [MIT License](LICENSE-MIT). Die
MIT-Option erlaubt die Einbindung in den GPL-2.0-only-Build von Freetz-NG.
Drittkomponenten behalten ihre jeweiligen Lizenzen. Siehe [NOTICE](NOTICE).
