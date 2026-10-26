# Projektregeln für KI-Agenten

## Zielplattformen

- Das primäre Ziel ist PS4 Homebrew.
- macOS ist der lokale Entwicklungsrechner.
- Linux/WSL ist der vorgesehene Build-Host.
- macOS nicht als unterstützten Build-Host annehmen, solange das Repository dies nicht ausdrücklich vorsieht.

## Sicherheitsregeln

- Keine proprietären SDKs, Schlüssel, Zugangsdaten oder persönlichen Pfade committen.
- Keine Dateien außerhalb des Repositorys verändern.
- Keine Build-, Flash-, Paketierungs- oder Löschbefehle ohne vorherige Bestätigung ausführen.
- Vor Änderungen zuerst den aktuellen Git-Status prüfen.
- Keine Änderungen direkt auf `main`; für Änderungen einen separaten Branch verwenden.

## Arbeitsweise

- Vor Änderungen die betroffenen Dateien und den geplanten Diff nennen.
- PS4-spezifische Änderungen klar von Host-Code trennen.
- Bestehende Build-Skripte und Toolchains nicht ohne Begründung ersetzen.
- Nach Änderungen passende Tests oder Build-Schritte vorschlagen, aber nicht automatisch starten.
- Bei unklaren Annahmen nachfragen statt raten.

## Merken / TODO

- **In-App-Updater per HTTP für den privaten Fork (kaeptnphl):** Updates sollen ohne `.pkg`-Neuinstallation über GoldHEN eingespielt werden können. Dafür einen HTTP-Server (auf emma oder Mac) bereitstellen, der die `DolphinPS4-v*-kaeptnphl-update.tar`-Archive ausliefert, und den Updater-Endpunkt in der App entsprechend konfigurieren. Details und Plan in `TODO.md`. Später umsetzen.
