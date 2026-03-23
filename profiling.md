# Ergebnis Profiling Textur upload

## Ziel

Ziel dieses Projektes ist es, einen optimalen Weg zu finden, Bilddaten von einer Kamera (Bonder) oder einer Datenbank (Geobonder) in eine Webgui zu bringen.
Dabei

- den Gui Thread so wenig wie möglich blocken
- so wenig wie möglich cpu Zeit verbrauchen, um den Bonder nicht zu beeinträchtigen
- keine unnötige GPU Zeit verbrauchen, falls eine KI auf der GPU rennt. 
- möglichst wenige Kopien der Bilddaten
- kein umkodieren (in json oder so)

## C++ über Webbridge

branch: `dev_peter_sharedmemory`
Übertragung der Bilddaten ist am Besten über shared memory. Es wird die WebView2 Shared Buffer API verwendet.
Der wrapper "webview" [https://github.com/webview/webview] exponiert diese Api leider nicht, drum greife ich direkt auf webview2 zu.

### webview2 Versionsnr

Die Shared Buffer api benötigt mindestens 1.0.1661.34, ICoreWebView2Environment12 und ICoreWebView2_17.

Die webview2 Instanz hab ich ihn object.h reingegeben.

### Test-setup

#### C++ Seite

In `MyObject::startVideo()` werden zuerst ein shared memory Bereich angelegt (ein `SharedFrameSender`Objekt). Dann werden Testdaten generiert (ein 1024*4096 Bild).
Dann wird ein eigener "Kamera" Thread gestartet.
Dieser kopiert ca 20mal/Sekunde einen Ausschnitt dieses Bildes in den shared memory Bereich (das Bild scrollt langsam) und postet danach ein event an den javascript code mittels `PostSharedBufferToScript()`.
Das Event hat folgende Metadaten:

- Breite und Höhe des shared memory
- Bildformat (immer RGBA eigentlich)
- Die framenummer (um zu schaun, ob man ein frame überspringen sollte)

Die Kosten auf der C++ Seite sind im 1-2stelligen us Bereich (1 memcopy von 4mb)

#### JS Seite

Der event Handler `onSharedBufferReceived` empfängt über ein Event names `sharedbufferreceived` (Wird von der WebView2 Api gefeuert, wenn `PostSharedBufferToScript()` aufgerufen wird) den Shared buffer aus C++. `PostSharedBufferToScript()` wird aufgerufen, sobald der Buffer fertig befüllt ist.
Der Buffer wird als `Uint8Array` interpretiert (ohne Kopie) und an die grafik api (webgpu, webgl, skia) als rohbilddaten weiter gegeben.

#### Verstopfungsbehandlung

Um Verstopfung zu behandeln, falls die Javascript seite mit dem Verdauen nicht schnell genug nachkommt, wird ein neues Event nur geschickt, wenn das vorige Event schon fertig ist. Das passiert in `MyObject.cpp::startVideo()`. Ist das notwendig (bei 20fps) und gscheit so?
Oder wär ein ringbuffer auf der js seite gscheiter, so dass in `onsharedBufferReceived` einfach das letzte Frame genommen wird?

### Nicht gewählte Alternativen

- __Übertragung über Websockets__: Wäre schnell genug, cross platform. Ein bisschen mühsamer als mit shared memory, eine kopie mehr und mehr CPU Last.
- __named pipes__: sollt auch mit webkitgtk (unter linux) und webview2 (windows) funktionieren.
- __shared GPU Memory__: Dadurch, dass in webview2 der C++ Code und der Javascript code in zwei verschiedenen Prozessen laufen, ist das nicht möglich. In Chrome Embedded framework würde das angeblich funktionieren, weil alles in demselben Prozess passiert [(https://code.google.com/archive/p/chromiumembedded/issues/1006] und [https://magpcss.org/ceforum/viewtopic.php?f=6&t=17521]. Aber: ob die Daten auf der CPU geshared und dann in js auf die GPU hochgeladen werden oder auf der C++ Seite hochgeladen und dann auf der GPU geshared werden, sollte nicht zu viel Unterschied machen.
- __Übertragung wie die anderen Daten (json und base64)__: viel zu langsam, weil kein binary format.

## Upload auf GPU in javascript

### WebGPU

ca. 2-3ms pro übertragenem Bild in einem Thread auf js.
Latenz ca. 15-20ms.

### WebGL

ca. 2-3ms pro übertragenem Bild in einem Thread
Latenz ca. 10-20ms

### Direkt mit Skia

ca. 3-4ms pro übertragenem Bild im main Thread.
Die Frage ist, ob es einen Vorteil gibt dadurch, dass das Hintergrundbild in derselben Engine und demselben Kontext gerendert wird wie die Gui darüber.
