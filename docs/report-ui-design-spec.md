# HTML Rapor UI Tasarim Sozlesmesi

Bu belge **ne gorunmeli** sorusunu yanitlar: free-run, hardware-trigger ve
software-trigger HTML raporlarinin gorsel tasarim kararlari.

Belge yalnizca **gorsellestirme** kapsamindadir: backend'den gelen sonuclari
alir ve nasil sunulacaklarini tanimlar. Verdict kurallari, olcum mantigi,
parametreler ve backend davranisi bu belgenin konusu degildir.

Iki belgelik yapinin ilk parcasidir; her karar **tek** bir dosyada yasar:

| belge | soru |
| --- | --- |
| `docs/report-ui-design-spec.md` (bu belge) | ne gorunmeli |
| `docs/implementation-plan.md` | ne yapilacak |

Ucuncu parca `docs/renderer-data-contract.md` idi; 2026-08-12'de kullanici
karariyla kaldirildi. "Hangi veri, hangi test" sorusunu artik
`docs/assets/refactored_previews/` icindeki 26 onayli preview yanitlar.
>
> **Software-trigger notu:** Software-trigger raporu hardware-trigger ile ayni
> kart yapisini kullanir. Fark yalnizca trigger timing etiketleri ve bazi
> testlerin SKIP durumudur. Bu belgede acikca belirtilmedikce hardware-trigger
> icin onaylanan gorsel yapi software-trigger icin de gecerlidir.

## Durumlar

- `INCELEME BEKLIYOR`: Bolum henuz degerlendirilmedi.
- `ONAY BEKLIYOR`: Degerlendirme ve onizleme tamamlandi; kullanici onayi
  bekleniyor.
- `TASARIM ONAYLANDI`: Tasarim karari ve onizleme onaylandi; kaynak kod
  degisikligi daha sonra yapilacak.
- `UYGULANDI`: Onaylanan tasarim kaynak koda uygulandi.
- `DOGRULANDI`: HTML, responsive gorunum ve print gorunumu test edildi.

## Calisma Kurallari

0.1. Bolumler asagidaki sirayla incelenecek.

0.2. Her bolum icin once yalnizca mevcut durum raporlanacak.

0.3. Kullanici degerlendirme istedikten sonra iyi taraflar, sorunlu noktalar
ve onerilen yeni duzen sunulacak.

0.4. Grafik icermeyen tasarim degisiklikleri icin birebir yapisal HTML veya
ASCII onizleme gosterilecek.

0.5. Grafik iceren her test icin gercek HTML/SVG onizlemesi olusturulacak ve
referans rapordaki gercek degerlerle render edilecek.

0.6. Kullanici onizlemeyi tarayicida acar ve gorsel dogrulama yapar.
Sonuc `OK` veya `NOT-OK` (geri bildirimle birlikte) olarak bildirilir.

0.6.1. `assets/refactored_previews/` dizininde **PNG/screenshot uretilmez**.
Dizin yalnizca HTML ve CSS tasir. Gorsel dogrulama 0.6'daki gibi kullanicinin
tarayicisinda yapilir; ajanin headless render alip PNG commit etmesi bu akisin
parcasi degildir. Uretilmis PNG varsa silinir.

0.7. Kullanici onayi alinmadan rapor uretim koduna tasarim degisikligi
uygulanmayacak.

0.8. Animasyon kullanilmayacak. Ekran tasarimi ve print ciktisi birlikte
degerlendirilecek.

## Section ve Grafik Sozlesmesi

Kullanici kararlari, 2026-08-07. 26 preview'in tamamina uygulandi;
verifier tarafindan zorunlu tutulur.

### S1. Section rolleri

Kart icinde yalnizca uc section adi bulunur:

| Section | Sutun imzasi | Kural |
| --- | --- | --- |
| `Measurement` | serbest (`Status` **yok**) | ham olcum, grafik, kanit tablolari |
| `Measurement Result` | `Metric \| Type \| Unit \| Value \| Detail \| Status` | **her** satirda verdict; `—` yasak |
| `Test Configuration` | `Variable \| Source \| Type \| Unit \| Value` | girdi parametreleri |

Bir tabloda hem verdict'li hem verdict'siz satir varsa tablo **ikiye
bolunur**; `Status` sutununa `—` yazilmaz.

### S2. Item alt basliklari

`Measurement` altindaki her grafik ve tablo kendi adini tasir
(`item-label`): 10px, `uppercase`, `font-weight:800`, 1px alt cizgi
(`#dfe5eb`).

- Ad **konuyu** soyler; `Graphic` / `Table` kelimesi **kullanilmaz**.
- Alt cizgi yalnizca `item-label`'a aittir; legend veya grafik cercevesi
  cizgi tasimaz.

### S3. Merdiven girintisi

Section basligi 21px → item basligi 53px → item icerigi 69px.
Girinti section'a eklenen `has-items` sinifiyla kapsanir ve **her**
icerik prefix'i (tablo satirlari **ve** grafik bar satirlari, legend,
`chart-frame`) selektore dahil edilir.

- Olcum `getBoundingClientRect().left` ile **yapilmaz** — o kutu kenarini
  dondurur, padding'i gizler. `document.createRange().selectNodeContents()`
  ile **metin** konumu olculur.

### S4. Unit sozlugu

`Unit` gercek bir olcu birimi veya `—`'dir.

- **Acik yazilir:** `milliseconds`, `microseconds`, `seconds`, `percent`,
  `hertz`, `bytes`, `kibibytes`, `mebibytes`, `gibibytes`, `pixels`,
  `frames per second`, `mebibytes per second`, `gibibytes per second`,
  `milliseconds per buffer`, `buffers per second`, `returns per frame`.
- **Kisaltma yasak:** `ms`, `%`, `B`, `Hz`, `MiB/s` bir **hatadir**.
  `Detail` hucresi istisnadir; orada cumle icinde `ms` gecebilir.
- **Sayilan nesne birim degildir:** `cycles`, `frames`, `buffers`,
  `delays`, `phases`, `rounds`, `captures`, `formats`, `controls`,
  `widths` → `Unit` = `—`. Sayilan sey zaten `Metric` adindadir.
- `Type` = `ratio` veya `string` ise `Unit` her zaman `—`.

### S5. Grafik zorunlulugu: bilgi kaybi testi

- Tablodaki veri **bilgi kaybi olmadan** grafige donusuyorsa grafik
  **zorunludur**.
- Donusmuyorsa grafik **opsiyoneldir** ve ancak gercekten onemli bir seyi
  dogru bir stille gorsellestiriyorsa eklenir.
- **Sequence / protokol diyagrami yasaktir.**
- **Olmayan deger cizilmez:** yer tutucu bar/rect konmaz ve karsilik gelen
  legend girdisi de kaldirilir. Bilginin kendisi tabloda kalir.

### S6. Renk

**Yesil yalnizca verdict icindir.** Notr ikinci seri mavi aileden gelir:
`#8fbcdb` (acik), `#3b82b6` (koyu).

- Ayrim **seride degil anlamdadir**: bir seri basari/basarisizlik ayrimi
  tasiyorsa yesil dogrudur (T06 `Pass ≥90%`, T25 `Successful rounds`,
  T26 `Stabilized within window`). Yalnizca "ikinci olcum" olan seri
  yesil olamaz — T14'te `headroom-free` ve T24'te artan gecikme boyle
  maviye cevrildi.

### S7. Grafik eksen adi

Grafigin alt-orta yazisi bir **eksen adidir ve birimini tasir**
(`Mean capture latency (milliseconds)`), item basliginin tekrari
**degildir**. Cumle de degildir.

- Parantez ici **S4'un `Unit` sozlugune bagli degildir**: eksen "bu eksende
  ne var" sorusunu yanitlar, dolayisiyla kategorik eksende sayilan nesne
  dogrudur (`Fresh session (cycle)`, `Warm-up length (frames)`). S4
  yalnizca tablo `Unit` **sutununu** baglar.
- Ayni grafik farkli modlarda farkli olcekte olabilir; eksen adi olceyi
  izler (T03: free-run `(milliseconds)` 0.010, trigger `(seconds)` 1.13).
- Olculen: 33 eksen adinin tamami birim/olcek parantezi tasiyor.

### S8. SVG olcek

`viewBox` genisligi **gercek render genisligine esit** olur; aksi halde
bildirilen `font-size` `renderWidth/viewBoxWidth` kadar carpilir.

- Olculen sapmalar: t16 **1.7423** (10px → 17.42px), t23/t26 **1.812**
  (8px → 18.12px), t14 1.804, t13 1.03.
- `aspect-ratio` de `viewBox` ile ayni olur; bayat `aspect-ratio` t13'te
  117px olu bosluk uretmisti.
- `text-transform:uppercase` SVG eksen basligina **sizmamalidir**.
- **Sabit bir viewBox genisligi yoktur; kapsayici genisligi belirler.**
  48px girintili `chart-frame` icinde 906, 16px girintili `chart-wrap`
  icinde 938 (T09). Olculen: 13 SVG'nin tamaminda olcek **1.0000**.
- Kanonik geometri (`chart-frame`, `viewBox 0 0 906 240`):
  Y ekseni `x=70`, `y 196→24`, ok ucu `(70,14)`;
  X ekseni `y=196`, `x 70→870`; veri `x 70..850`; y tik `x=62 anchor=end`;
  x tik `y=212`; x adi `y=232 anchor=middle`;
  y adi `rotate(-90,18,110)`. Yukseklik icerige gore 190/200/240 olabilir.

### S9. CSS hijyeni

- Bir `X-header,X-row` grid tanimi **iki kez** yazilmaz; ikincisi kazanir
  ve hucreler yanlis sutuna duser (t20'de 5 sutun 4'e coktu).
- Kullanilan her gorsel sinifin CSS'i **tanimli** olmali; tanimli her
  kural da **kullanilmis** olmali. Olu CSS de sapmadir, eksik CSS de.
- Selektor satiri `{...}` blogu olmadan birakilmaz: CSS ayristirici onu
  sonraki blokla birlestirir ve stil sessizce yanlis elemana uygulanir
  (t16'da legend'a tablo alt cizgisi boyle sizdi).

### S10. Hucre metni sarmaz

Tablo hucresi `white-space: normal` oldugu icin uzun metin **tasmaz,
sarar** ve satir yuksekligini bozar. Bu yuzden tasma taramasi
`scrollWidth` ile **yapilamaz**; `Range.getClientRects().length > 1`
ile olculur.

## Gorsel Tasarim Prensipleri

Uygulandi ve testle kilitli (`report_card_contract`). Ayrintili gerekce
metinleri kaldirildi; kurallar asagida.

**Yazi olcegi.** Uc kademe: icerik 9px < sutun sayisi basligi 10px < section
basligi 11px. Sutun basligi ayrica `800` ve `uppercase`. `font-size`
yazilmamis olmak notr degildir — `<h3>` tarayici varsayilanina (18.72px) duser
ve olcek sessizce bozulur.

**Tablolar `div`+CSS grid ile kurulur, `<table>` ile degil.**

- Baslik ve veri satirlari **ayni** `grid-template-columns` degerini paylasir.
- Yatay padding **satirin kendisinde** olur (`padding: 6px 16px`), hucrede
  degil. `<table>` + `th/td` padding'inde sutunlar iceri kayar, tablo kart
  kenarina dayanir.
- Sutunlar **esit genislikte** (`1fr` x N). Sabit `px` sutunlar arasi boslugu
  esitsiz gosterir.
- **Her sutun sola yaslidir, son sutun dahil.** Istisna yok. Grafiklerin bar
  satirlarindaki sayisal etiket (bir `-header` kardesi olmayan yapilar) saga
  dayali kalir; orada hizalama islevseldir.
- Baslik satiri veri satirlarindan `border-bottom: 1px solid #aeb9c5` ile
  ayrilir.

**Tablo yalnizca sonuc ve veri tasir; yorum, oneri veya aciklayici ifade
tasimaz.** Bir testin sonucu verilir; ne yapilmasi gerektigi soylenmez.
`Recommended discard`, `Extend the window` gibi tavsiye eden metric adi
kullanilmaz. Tavsiye eden bir baslik gorulurse once **altindaki degerin
gercekte ne oldugu** olculur: `Recommended discard` aslinda gozlenen en uzun
warm-up degerini tasiyordu ve `Longest warm-up` olunca bilgi kaybi olmadi.

**Hucre vurgusu.** Tablo satirindaki HER hucre duz `#2d3a47`, `font-weight`
normal, 9px. Tek istisna verdict tasiyan hucredir. Vurgu dort yoldan sizar,
dordu de yasaktir: hucre sinifinda `font-weight`/farkli `color`; `X-header,
X-row` **taban** kuralindaki renk (sinifsiz hucreler onu miras alir);
`-status` taban sinifinda kalinlik; kucultulmus `font-size`.

Kural sinif **adina** degil hucre **icerigine** baglanir: bir sinifin verdict
tasidigi `PASS|WARN|FAIL|SKIP` metninden anlasilir, boylece `-outcome` gibi bir
ada burunerek vurgu geri getirilemez.

**Value hucresi ham veridir.** Sayilar, enum adlari, timing kaynagi
(`MONOTONIC`, `10/10`). Buyuk harfli olsa da statu degildir, statu rengi almaz.
Value'da buyuk harfli bir **statu kelimesi** duruyorsa kelime yanlis sutundadir:
verdict ise `Status`'a gecer, degilse gercek bir degere cevrilir veya satir
kaldirilir. Renk kaldirip kelimeyi yerinde birakmak sorunu gizler.

Gercek cihaz enum'lari (`MMAP`, `MONOTONIC`, `YUYV`, `DMABUF`) veridir ve
Value'da kalir. Bu listede olmayan buyuk-harfli bir token uydurmadir.

**Yasaklar.**

- Basliksiz yatay metric bandi (`summary-row` + `metric-label`/`metric-value`).
- `Metric Definitions` sozluk bolumu.
- Raw detail bloklari (RECORDED VALUES, monospace dump).
- Bolumler **arasina** serbest cumle blogu (`.note`).
- Ayni bilgiyi ikinci kez soyleyen legend.
- Uydurma statu degeri (`ACCEPTED` vb.). Beklenen davranis gozlenirse `PASS`.
- `Flags: none` gibi yoklugu yazan satir; yokluk zaten bilgidir.

**Result blogu.** PASS kartinda gosterilmez. Class adi statuye baglidir:
`result-fail` / `result-warn` / `result-skip`; generic `result` **kullanilmaz**
ve CSS'te tanimlanmaz. Metin prefix'i de statuye baglidir (`Failed:` /
`Warned:` / `Skipped:`). Kart statusu ile class birebir eslesir.

Paleti: `result-fail` `#6e2424` / `#fff7f7`; `result-warn` `#6e4f00` /
`#fffdf5`; `result-skip` `#596776` / `#f8f9fa`. Ortak yapi
`padding:12px 16px; font-size:12px; font-weight:600`.

Gerekce: generic `result` iki dosyada iki farkli renge baglanmisti ve t04'te
WARN kartini **gri** render ediyordu. Dogrulama iki yonludur: kullanilan her
`result-*` tanimli olmali **ve** tanimli her kural kullanilmis olmali.

**Esik cizgisi.** Legend'daki soyut esik grafigin uzerinde kesikli dikey cizgi
(`thr-line`) ve konum etiketi (`thr-mark`) olarak gorunur. Yuzde konum tasir
(`left: 90%`), sabit `px` degil. Etiketsiz cizgi kullanilmaz. Olculen deger ile
esik arasinda buyuk buyukluk farki varsa (T03 barlari `0.016 ms`, esik
`500 ms`) cizgi eklenmez; esik `Test Configuration`'da kalir. Sayisal esigi
olmayan testlerde (T11, T12, T15) uydurma esik gosterilmez.

**Bar grafiginde uzunluk degeri temsil eder**, ince isaretci degil. Bar `0`'dan
kendi degerine dolar (`width: val / scale-max * 100%`). Her barin etiketi
solunda ve degeri saginda duruyorsa legend eklenmez.

**Eksen olcegi dogrusaldir**: `x = X0 + (v - vmin) / (vmax - vmin) * W`.
Noktalar goze hos gorunecek sekilde elle yerlestirilmez. Dogrusal olmayan olcek
etiket cakismasi uretir ve bu cakisma genellikle etiketleri farkli `y`'ye
kaydirarak gizlenir — basamakli gorunen bir eksen, altinda yatan olcek hatasinin
belirtisidir.

**Grafikte hem X hem Y ekseni bulunur**, ikisi de birim basligi tasir. Isaretli
noktalar kesikli cizgiyle her iki eksene baglanir (`stroke-dasharray`), rengi
legend'daki nokta rengiyle ayni. Bu cizgi **esik cizgisi degildir**.

**Test Configuration** 5 sutunlu: `Variable | Source | Type | Unit | Value`.
`Source`: `param` / `threshold` / `derived` / `fixed`. Parametresiz probe
testlerinde (T01, T02) bu bolum bulunmaz.

## 1. Rapor Ust Bolumu — ARSIV

Durum: `UYGULANDI` (2026-08-08). Ust bolum, metadata kartlari ve export
toolbar uygulandi; sozlesme `tests/export_toolbar_test.cpp` ve
`tests/report_dom_audit_test.cpp` ile kilitli.

Kalici hukumler:

- **Export toolbar**: dort kontrol subtitle altinda ayri satirda, masaustunde
  saga hizali. `Export PDF` `window.print()` cagirir; JSON, Markdown ve DMESG
  onceden uretilmis dosyalara **goreli link**. Print'te tamami gizli.
- **DMESG** uretilemezse kontrol **hic gosterilmez** — yoktan bir href,
  tiklayinca bulunan bir 404'tur.
- **Free-run**: `Trigger Profile: Not required (free-run)`.
- Metadata kartlari: Session, System, Camera, Trigger.

## 2. Result Distribution

Durum: `TASARIM ONAYLANDI`

> **Not:** Gorunus olarak dogru calisiyor. Bu kisminda code review yapilacak.
> Amac varsa optimize edilecek veya gozden kacan hatalari duzeltmek.
> Status renkleri `Test Results Overview` ile eslestirilecek (§4).

## 3. Test Results Overview

Durum: `TASARIM ONAYLANDI`

> **Not:** Gorunus olarak dogru calisiyor. Bu kisminda code review yapilacak.
> Amac varsa optimize edilecek veya gozden kacan hatalari duzeltmek.
> Status renkleri bu bolum referanstir; diger bolumler buna eslestirilecek (§4).

## 4. Detailed Results ve Test Karti Yapisi

Durum: `TASARIM ONAYLANDI`

> **Not:** Genel yapi dogru calisiyor. Asagidaki bekleyen maddeler (result
> giris formati ve renk eslestirmesi) refactor edilecek, ardindan code review
> yapilacak. Amac varsa optimize edilecek veya gozden kacan hatalari duzeltmek.

Kararlar:

- Backend'e gore gruplama; her backend bandi bir kez, test kartlari girintili.
- Detailed Result Card Template: sol vurgu cizgisi, `STATUS | Txx - Ad | Duration`.
- Status: `PASS / WARN / FAIL / SKIP` (nokta/ikon yok).
- Duration: rapor ve web UI **ayni** formatlama sozlesmesini kullanir
  (52 ortak vektor, `tests/data/duration_format_vectors.txt`).
- Anchor: `result-<backend>-<test_id>` (kamera dahil degil).
- Trigger modu her kartta tekrarlanmaz; raporun ust bilgisinde (§1) zaten var.

**Result giris kurali:**

- `PASS`: Giris kisminda hicbir yazi yazilmaz.
- `WARN`: `Warned: <kisa ve net ifade>`
- `FAIL`: `Failed: <kisa ve net ifade>`
- `SKIP`: `Skipped: <kisa ve net ifade>`

**Status renk tutarliligi (§4):**

Tum rapor bolumleri (Result Distribution, Overview, Detailed Results) ayni
PASS/WARN/FAIL/SKIP renklerini kullanacak. Referans: Test Results Overview.

Uygulandi (2026-08-08), uretilen rapor uzerinde olculdu:

- [x] Giris metni: `Warned:` ×10, `Failed:` ×4, `Skipped:` ×2; PASS kartlarinda
      result blogu **yok**.
- [x] Status rengi tek kaynaktan. Uc bolum (Overview, kart basligi, verdict
      hucresi) uc farkli palet tasiyordu — ayni PASS uc farkli yesil
      goruntuluyordu. Hepsi `var(--pass|--warn|--fail|--skip)`'e baglandi;
      canli render'da dort durumda da uc bolum **ayni** renk. `report_card_contract`
      literal hex'e donusu yakalar (sabotajla kanitlandi).

## 5. Test Bazli Detayli Sonuc Incelemeleri — ARSIV

Durum: `UYGULANDI` (2026-08-08). 26 testin tamami icin tasarim onaylandi ve
renderer'a uygulandi.

**Bu bolumun ayrintili metni kaldirildi.** Teste ozgu tasarim kararlari artik
iki yerde yasiyor ve ikisi de bu metinden daha guvenilir:

- `docs/assets/refactored_previews/t01..t26-preview.html` — 26 onaylı preview,
  uc trigger modu. Tasarimin **kendisi**, tarifi degil. Her testin bolumleri,
  tablo sutun imzalari ve metric adlari buradan okunur.

Sozlesme ayrica testle kilitli: `report_card_contract` (S1-S8 yapisal
sozlesme, 26 testi kapsayan fixture) ve `metric_name_contract` (renderer'in
okudugu her metrik adi runner tarafindan uretiliyor mu).

| test | ad | durum |
| --- | --- | --- |
| T01 | V4L2 Device Compliance | `UYGULANDI` |
| T02 | V4L2 Control Inventory | `UYGULANDI` |
| T03 | Pipeline Readiness after STREAMON | `UYGULANDI` |
| T04 | Frame Capture without STREAMON | `UYGULANDI` |
| T05 | STREAMOFF Error Handling and Recovery | `UYGULANDI` |
| T06 | STREAMON/STREAMOFF Cycle Reliability | `UYGULANDI` |
| T07 | Multi-buffer Configurations | `UYGULANDI` |
| T08 | Buffer Saturation Behavior | `UYGULANDI` |
| T09 | Buffer Requeue Delay Tolerance | `UYGULANDI` |
| T10 | V4L2 Buffer Flag Analysis | `UYGULANDI` |
| T11 | Memory Access Throughput | `UYGULANDI` |
| T12 | DMABUF CPU Read Synchronization | `UYGULANDI` |
| T13 | Poll Timeout Reliability Boundary | `UYGULANDI` |
| T14 | Trigger-to-Frame Delivery Latency | `UYGULANDI` |
| T15 | Non-blocking Spin vs Blocking DQBUF | `UYGULANDI` |
| T16 | Trigger Pulse Width and Edge Detection | `UYGULANDI` |
| T17 | Pixel Format Performance Comparison | `UYGULANDI` |
| T18 | V4L2 Control Value Impact Analysis | `UYGULANDI` |
| T19 | Resolution Capability and Performance | `UYGULANDI` |
| T20 | Frame Sequence Continuity | `UYGULANDI` |
| T21 | Buffer Timestamp Monotonicity | `TASARIM ONAYLANDI` |
| T22 | Consecutive Frame Content Stability | `TASARIM ONAYLANDI` |
| T23 | Sustained Capture Stability | `TASARIM ONAYLANDI` |
| T24 | CPU Load Impact on Capture Latency | `TASARIM ONAYLANDI` |
| T25 | Multi-camera Capture and Synchronization | `TASARIM ONAYLANDI` |
| T26 | Post-STREAMON Latency Stabilization | `TASARIM ONAYLANDI` |

## 6. Entegre HTML ve Print Dogrulamasi — ARSIV

Durum: `UYGULANDI` (2026-08-08). Anchor butunlugu, pagination, print CSS ve
responsive davranis uygulandi ve olculdu.

Kalici hukumler:

- **PDF uretilmez.** `Export PDF` tarayicinin print diyalogunu acar; uygulama
  running footer uretmez.
- **Sayfa geometrisi tek kaynaktan**: `@page { size: A4; margin: 12mm 10mm 14mm }`.
  Print ek yatay padding eklemez — ikinci bir kaynak kullanicinin diyalog
  secimiyle carpisir.
- **Dort viewport** (1440/1024/768/390): document yatay kaymaz, grafik ve
  legend kirpilmaz, test-header hucreleri cakismaz, uzun identifier parent'i
  buyutmez. Uc trigger modunda 12/12 olculdu (implementation-plan §5.3).
- **Grafik kendi kutusunda kayar**, kucultulmez: kucultmek 8px etiketi de
  olceklerdi.
- Uzun tablolarda `<thead>` print devam sayfalarinda tekrarlanir.

### 6.6. Continuation header — UYGULANMIYOR (olculdu 2026-08-08)

Uzun bir kartin devam sayfasi `T16 - ... | continued` bicimli compact bir
header ile baslamaliydi.

**Onceki teshis yanlisti.** "Guvenilir primitifler §6.9 tarafindan disarida
birakildi" diye kaydedilmisti; olcum bunu curuttu. Chrome 146'da uc yolun
uctu de kapali:

| yol | Chrome destegi | olcum |
| --- | --- | --- |
| `position: running()` + `@page { @top-center }` | **yok** | `CSS.supports` `NO`, `position` `static` kaliyor |
| `string-set` / `content: element()` | **yok** | `CSS.supports` `NO` |
| `display: table-header-group` (div uzerinde) | var ama **ise yaramiyor** | iki sayfalik kartta baslik **bir kez** cizildi |

Yani §6.9'un "running footer uretilmez" hukmu bunu engellemiyordu — o hukum
sayfa numarali altbilgi hakkinda, CSS'in `running()` ozelligi hakkinda degil.
Kural gevsetilse de sonuc degismezdi.

**Calisan tek mekanizma gercek `<table>` + `<thead>`:** ayni testte baslik iki
sayfada da cizildi (`continued` ×2). Ama kartlari `<table>` yapmak
"Tablolar `div`+CSS grid ile kurulur, `<table>` ile degil" hukmunu bozar — o
hukum sutun hizalamasi ve padding davranisi icin var ve 26 preview onun uzerine
kurulu.

**Sonuc:** madde kapatildi. Statik bir eleman "sayfa kirilmasinin dustugu
yerde" gosterilemez; her kartin basina konup ekranda gizlendiginde print'te
**bolunmemis** kartlarda da gorundu (26 kartin 20'sinde `| continued`), yani
okuyucuya kesilmemis bir sayfanin kesildigini soyluyordu. Eksik olmasindan
kotu oldugu icin kaldirildi ve testle kilitlendi.

Uzun **tablolarda** `<thead>` tekrari calisiyor ve korunuyor; kart duzeyinde
karsiligi yok.
