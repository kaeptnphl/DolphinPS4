# TODO

## In-App-Updater per HTTP (privater Fork kaeptnphl)

**Ziel:** Updates (neue Builds) sollen ohne Neuinstallation des `.pkg` über GoldHEN eingespielt werden können – direkt aus der App heraus.

**Idee:**
- Dolphin hat einen eingebauten Updater (`PS4Updater.h/cpp`). Die App lädt ein Update-Archiv (`DolphinPS4-v*-kaeptnphl-update.tar`) von einer HTTP-URL herunter und entpackt es nach `/data/DolphinPS4/app` (eboot.bin, xmb/, Sys/). Beim nächsten Start läuft die neue Version.
- Dafür einen HTTP-Server bereitstellen (auf emma oder Mac), der die `.tar`-Archive ausliefert.
- Den Updater-Endpunkt in der App entsprechend konfigurieren (URL auf den Server).

**Status:** Später umsetzen. Bei Bedarf erinnern: "In-App-Updater einrichten".

**Referenz:** `scripts/package-dolphin.sh` erzeugt das Update-Archiv (`DolphinPS4-v$VERSION-update.tar`). Der Updater liest die neueste Release-Info aus dem Trace-Log (`update: latest release 04.19 (...)`).
