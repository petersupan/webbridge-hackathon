# Ergebnis Profiling Textur upload

## C++ über Webbridge

branch: `dev_peter_sharedmemory`
Übertragung der Bilddaten ist am Besten über shared memory. Es wird die WebView2 Shared Buffer API verwendet.
Der wrapper "webview" [https://github.com/webview/webview] exponiert diese Api leider nicht, drum greife ich direkt auf webview2 zu.

#### webview2 Version

Die Shared Buffer api benötigt mindestens 1.0.1661.34 ICoreWebView2Environment12 und ICoreWebView2_17.

Die webview2 Instanz hab ich ihn object.h reingegeben.

### Test-setup

#### C++ Seite

In `MyObject.cpp::startVideo()` werden zuerst ein shared memory Bereich angelegt (ein `SharedFrameSender`Objekt. Dann werden Testdaten generiert (ein 1024*4096 Bild).
Dann wird ein eigener "Kamera" Thread gestartet, der ca. 20mal/Sekunde einen Ausschnitt dieses Bildes in den shared memory Bereich kopiert (das Bild scrollt langsam).

#### JS Seite

Der event Handler `onSharedBufferReceived` empfängt den Shared buffer aus C++ (wird aufgerufen, sobald der Buffer fertig befüllt ist).
Der Buffer wird als `Uint8Array` interpretiert (ohne Kopie) und an die grafik api (webgpu, webgl, skia) als rohbilddaten weiter gegeben.

#### Verstopfungsbehandlung

Um Verstopfung zu behandeln, falls die Javascript seite mit dem Verdauen nicht schnell genug nachkommt, wird ein neues Event nur geschickt, wenn das vorige Event schon fertig ist. Das passiert in `MyObject.cpp::startVideo()`. Ist das notwendig (bei 20fps) und gscheit so?
Oder wär ein ringbuffer auf der js seite gscheiter, so dass in `onsharedBufferReceived` einfach das letzte Frame genommen wird?

### In Betracht gezogene Alternativen

- __Übertragung über Websockets__: Wäre schnell genug, cross platform. Ein bisschen mühsamer als mit shared memory, eine kopie mehr und mehr CPU Last.
- __named pipes__: sollt auch mit webkitgtk (unter linux) und webview2 (windows) funktionieren.
- __shared GPU Memory__: Dadurch, dass in webview2 der C++ Code und der Javascript code in zwei verschiedenen Prozessen laufen, ist das nicht möglich. In Chrome Embedded framework würde das angeblich funktionieren. Aber: ob die Daten auf der CPU geshared und dann in js auf die GPU hochgeladen werden oder auf der C++ Seite hochgeladen und dann auf der GPU geshared werden, sollte nicht zu viel Unterschied machen.
- __Übertragung wo wie die anderen Daten (base64)__: viel zu langsam, weil kein binary format.

- CEF offscreen rendering verwenden (meh.. lieber nicht) [(https://code.google.com/archive/p/chromiumembedded/issues/1006] und [https://magpcss.org/ceforum/viewtopic.php?f=6&t=17521] )

## Upload auf GPU in javascript

Die Kosten auf der C++ Seite sind im 1-2stelligen us Bereich (1 memcopy von 4mb)

### WebGPU

ca. 2ms pro übertragenem Bild in einem Thread auf js.

### WebGL

ca. 2ms pro übertragenem Bild in einem Thread

### Direkt mit Skia

ca. 3-4ms pro übertragenem Bild im main Thread.
Die Frage ist, ob es einen Vorteil gibt dadurch, dass das Hintergrundbild in derselben Engine und demselben Kontext gerendert wird wie die Gui darüber.
