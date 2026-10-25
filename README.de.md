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

> [!IMPORTANT]
> Dies ist unabhängige, experimentelle Community-Software. Die erste Version
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
| `light` | dimmbare Lampe oder Farbtemperaturlampe | Ein/Aus, Helligkeit, optional Farbtemperatur |
| `cover` | Rollladen | Öffnen, Schließen, Stoppen, Position |
| `climate` | Heizkörperthermostat | Modus, Solltemperatur, Wärme-/Kalt-Timer, optional Anzeige der nächsten Zeitplanänderung |

FRITZ!OS bleibt der Layout-Editor für das 440. Home Assistant verwaltet die
Verknüpfung zwischen virtuellem FRITZ!-Gerät und echter Entität. Ein virtuelles
Gerät kann auf mehreren Bedieneinheiten liegen. Beim Austausch einer
Home-Assistant-Entität können FRITZ!-Identität und vorhandene 440-Zuweisungen
erhalten bleiben.

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
inzwischen in beide Richtungen geprüft. Version 0.2.0-beta.1 enthält die
gesammelten Verbesserungen an Wiederherstellung, Katalogabgleich, Parsern und
Lebenszyklus sowie die vollständige Umbenennung zu TentAHAcle. Diese Beta hat
die Repository-Tests bestanden, wurde mit dem physischen Bedienteil aber noch
nicht erneut vollständig qualifiziert. Die exakten Daten stehen unter
[Kompatibilität](docs/compatibility.md).

### Projektstand und Mitmachen

Das Projekt ist eine funktionierende experimentelle Beta und noch kein fertiges
Produkt. Die unterstützten Profile, dauerhafte Geräteverwaltung,
MQTT-Wiederherstellung und der direkte 440-Befehlspfad besitzen automatisierte
Tests und wurden auf dem beschriebenen Laboraufbau praktisch geprüft. Besonders
hilfreich sind längere Praxistests, Tests mit weiteren FRITZ!OS-Versionen und
Geräten, bessere Home-Assistant-Diagnosen, Parser-Fuzzing und eine weitere
Aufteilung der nativen MQTT-Bridge. Nachvollziehbare Ergebnisse bitte über
[GitHub Issues](https://github.com/Allcrafter1/tentahacle-virtual-device-bridge/issues) melden.

Dieses Repository verteilt keine AVM-Firmware, keine AVM-Programme und keine
modifizierten Firmware-Images. Jeder Nutzer baut sein Image lokal selbst. Lies
vorher die [Lizenz- und Distributionsgrenzen](docs/licensing.md).

## Technische Dokumentation

- [Installation, Update und Wiederherstellung](docs/installation.de.md)
- [Architektur und Bedienablauf](docs/architecture.md)
- [Kompatibilitätsregeln](docs/compatibility.md)
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
