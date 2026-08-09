# Uygulama Plani — Web UI Denetimi ve Rapor Kararlari

Bu belge, web arayuzunun menu menu yapilan derinlemesine denetiminde bulunan
sorunlari ve `docs/report-ui-design-spec.md` ile sonuclandirilan rapor
kararlarini tek bir uygulama sirasinda birlestirir.

Kapsam: SPA (`source/frontend`), web sunucusu (`source/backend/web`), rapor
ureticisi (`source/backend/core/report_writer.cpp`), test/threshold registry'leri
ve bunlarin dokundugu hw davranislari.

Uc belgelik yapinin ikinci parcasidir — bu belge **ne yapilacak** sorusunu
yanitlar. Tasarim otoritesi `docs/report-ui-design-spec.md`, veri sozlesmesi
`docs/renderer-data-contract.md`; bu belge onlari uygulamaya cevirir ve
onlarla **celisemez**. Bir tasarim kurali buraya kopyalanmaz, atif verilir.

Durum (2026-08-08): Faz 0-5 uygulandi; kalan is §6'da. Bitirilen fazlarin
sureç kaydi arsiv ozetine indirildi — belge 2836 satirdan buraya kisaldi.

Backend: 31 CTest hedefi. Frontend: 156 Vitest testi.

Bu belgedeki tum atiflar **bolum numarasi** ile verilir; satir numarasi
kullanilmaz — hedef belge duzenlendikce bayatlar.

## Durumlar

- `ACIK`: Dogrulandi, uygulanmayi bekliyor.
- `UYGULANDI`: Kaynak koda uygulandi.
- `DOGRULANDI`: Test edildi.
- `DUSTU`: Alinan kararla birlikte madde ortadan kalkti.

## Calisma Kurallari

0.1. Her madde tek bir kok nedene baglanir; ayni kok nedenin farkli
belirtileri ayri madde olarak uygulanmaz.

0.2. Davranis degistiren her madde once o davranisi kilitleyen bir testle
gelir. Frontend'de su an hic test kosumu yok — bu yuzden F1 diger frontend
maddelerinden once gelir.

0.3. Zaten dogru calisan mekanizmalar yeniden yazilmaz; mevcut dogru desen
yeniden kullanilir. Ornekler: atomik dosya yazimi icin
`ProfileRegistry::write_profile_file`, erisilebilir secim icin
`SelectableCard`, bayat id migrasyonu icin `threshold_registry`'nin suffix
esleme algoritmasi.

0.4. Preview'ler onaylanmadan uygulama koduna gecilmez (bkz. Faz 1 kapisi).

---

## Calisma ve Karar Koruma Protokolu

Amac: onaylanmis kararlarin yorumlanarak degistirilmemesi.

### P1. Decision Lock — her adimdan **once**

Kod veya preview uretmeden once o adim icin kisa bir sozlesme yazilir. Icerik
plan belgelerindeki ve kullanicinin son mesajindaki **ifadelerden** cikarilir:

```text
DECISION LOCK
- Degistirilecek:
- Aynen korunacak:
- Yerlesim sirasi:
- Zorunlu metinler:
- Yasaklanan / karari reddedilen alternatifler:
- Onay gerektiren noktalar:
- Kaynak plan bolumleri:
```

### P2. Onaylanmis karar optimize edilmez

Bir tasarim veya davranis acikca secildiyse, "daha iyi" goruneni uygulanmaz.
Reddedilmis alternatiflerden biri **hicbir kosulda** uygulanmaz. Teknik bir
problem cikarsa kendi tercihe gecilmez: **durulur ve secenekler sunulur**.

Ornek — export toolbar (Faz 1 arsivi, design-spec §1):

| onaylandi | reddedildi |
| --- | --- |
| H1 → subtitle → saga hizali toolbar → metadata kartlari | toolbar'in H1 uzerinde olmasi |
| | toolbar'in ortalanmasi |
| | `position: fixed` / `absolute` |
| | viewport disina tasan toolbar |
| | aciklama metninin kaldirilmasi |
| | H1'in `max-width` ile daraltilmasi |

### P3. Celiski varsa uygulama yapilmaz

Iki gereksinim ayni anda saglanamiyorsa: biri sessizce kaldirilmaz, metin
degistirilmez, yerlesim kendi basina secilmez, plan yeni secime gore
guncellenmez. Once **celiski olcumlerle raporlanir ve karar istenir**.

### P4. Scope Freeze

Her gorevde yalnizca acikca istenen yuzeyler degisir. Ornek — toolbar gorevi:

| degisebilir | degisemez |
| --- | --- |
| export toolbar markup | H1 metni ve stili |
| toolbar responsive CSS | subtitle |
| print visibility | metadata icerigi |
| export linkleri | kart olculeri, rapor genisligi |
| | test kartlari, onayli diger preview icerikleri |

Kapsam disi bir hata bulunursa **duzeltilmez**; ayri bulgu olarak raporlanir.

### P5. Conformance Matrix — her adimdan **sonra**

"Tamamlandi" demeden once doldurulur. Bir satir PASS degilse is tamamlanmis
sayilmaz.

| Onayli karar | Uygulamadaki karsiligi | Kanit | Sonuc |
| --- | --- | --- | --- |

### P6. Statuler kendiliginden yukseltilmez

| gecis | kosul |
| --- | --- |
| → `ONAY BEKLIYOR` | preview uretildi |
| → `TASARIM ONAYLANDI` | **kullanici acikca onayladi** |
| → `UYGULANDI` / `DOGRULANDI` | kod ve test tamamlandi |

Preview kendi degerlendirmeyle `TASARIM ONAYLANDI` yapilmaz.

### P7. Karar otoritesi sirasi

1. Kullanicinin en son acik karari
2. `docs/implementation-plan.md`
3. `docs/report-ui-design-spec.md`
4. Mevcut kaynak kod
5. Kendi onerim

Ust sira alt siradan onceliklidir. Plan ile son kullanici karari celisiyorsa
once **plan guncelleme onerisi** sunulur; sessizce biri secilmez.

### P8. Sabotaj testleri karar uyumunu da kapsar

Yalnizca kodun calistigi degil, **reddedilen duzenin geri gelmedigi** de test
edilir. Ornek guard'lar — her biri sabote edildiginde test **dusmelidir**:

- toolbar H1'den once tasinirsa,
- toolbar `center` yapilirsa,
- DMESG aciklamasi kaldirilirsa,
- `position: fixed` geri gelirse,
- Report Formats navigation'a geri eklenirse.

---

## Faz 0-2 — TAMAMLANDI (arsiv)

Durum: `UYGULANDI`. Uc faz da kontrol listesinde tam isaretli; ayrintili
uygulama notlari kaldirildi, kalici hukumler asagida.

| faz | kapsam | tarih |
| --- | --- | --- |
| **0** | Plan belgelerini gecerli hale getirme (kod yok) | 2026-08-03 |
| **1** | Preview'lerin yeniden uretimi — ONAY KAPISI gecildi | 2026-08-03 |
| **2** | Backend temelleri: P0 duzeltmeleri, PDF kaldirma, test registry, config versiyonlama, rol tabanli trigger, yapisal sonuc endpoint'i, wire kontratlari, Pass Rate, sunucu hijyeni, zorunlu uc artifact (sema v5) | 2026-08-03/04 |

Sonraki fazlarin atifta bulundugu kalici hukumler:

### §1.3 — Grafik denetimi

Bir grafik ancak onaylı preview'inde `<svg>` tasiyan test icin cizilir.
Chart selector `_mean_ms`/`_p95_ms`/`_max_ms` ailesini otomatik sectigi icin
sekiz test preview'inde bulunmayan nokta grafigi uretiyordu; izin listesi
(`test_charts_approved`) preview setinden turetildi.

### §1.7 — Export toolbar

Durum: `UYGULANDI` (2026-08-08). Tasarim 2026-08-04'te onaylandi; kaynak kod
uygulamasi §3.3 ve §3.4 ile tamamlandi ve uretilen rapor uzerinde olculdu.

Dort kontrol subtitle'in altinda ayri satirda, masaustunde saga hizali. Dort
export da onceden uretilmis dosyaya **goreli link**; print'te tamami gizli.
Baslik, metadata veya raporun baska bolumu degistirilmez.

**Onaylı preview dosyalari artik yok:** `docs/assets/previews/` dizini
silinmisti. Sozlesme metinde ve testte yasiyor
(`tests/export_toolbar_test.cpp`).

**"Archived" gorunumu ortadan kalkti:** o hal DMESG'in sunucu bagimli
olmasindan doguyordu. 3.4 DMESG'i uretilen artifact'e cevirince `file://`
uzerinde de dort kontrolun dordu de calisir oldu.

### §2.3 — Test registry: adlar ve layer

`TestLayer` enum'u (`Discovery=1` ... `Stability=7`) ve `TestDefinition.layer`
alani. `layer_number()` (1-7, siralama) ve `layer_name()` (kisa ad) sunum
katmanina `Layer 3 — Buffer & memory` basligini kurar.

`layer`, teknik filtreleme anahtari `category`'den **ayri** bir kavramdir;
ikisi de korunur.

### §2.10 — Zorunlu uc artifact

Her run HTML + JSON + Markdown yazar (sema v5). `report_formats` secimi
kaldirildi. Artifact yazimi sessizce basarili olamaz: bir format yazilamazsa
run basarisiz olur, cunku eksik bir dosyayi var gibi gostermek daha kotudur.

DMESG bu sete 3.4 ile **kosullu** olarak katildi: yazilabildiginde yazilir,
yazilamadiginda rapor yine tamdir ve kontrol hic gosterilmez.

## Faz 3 — Rapor ureticisi

Durum: `UYGULANDI` (2026-08-08) — 3.1-3.6 tamam. Kontrol listesi 3.1/3.2/3.3/
3.5/3.6'yi isaretsiz tasiyordu; uretilen rapor uzerinde tek tek olculup
isaretlendi:

| madde | olcum |
| --- | --- |
| 3.1 | 5 test / 2 backend → backend basina **bir** band; ortak kabuk `render_result_card_open/close` |
| 3.2 | 52 ortak vektor (`tests/data/duration_format_vectors.txt`), C++ ve Vitest ikisi de gecti |
| 3.3 | `>Export PDF<` ×1, `Export as PDF` yok; JSON/MD goreli link |
| 3.4 | otomatik artifact (yukarida) |
| 3.5 | uc artifact + dmesg ayni kanonik tabani paylasiyor |
| 3.6 | `Not required (free-run)`, `>Warned<`, `>SKIP<` ×4, `SKIPPED` yok |

### 3.1. Detailed Result Card Template

Tek ortak component/renderer. **24 test preview'inin icerigi ortaklastirilmaz**:
yalnizca **dis** sablon ortak renderer'dan gelir; grafik, tablo, metric definition
ve test aciklamalari daha once onaylanan **teste ozgu icerik** olarak korunur.

Kaynak renderer ile preview arasinda fark cikarsa once siniflandirilir — (a) veri
farki, (b) ortak sablon farki, (c) teste ozgu icerik farki. Yalnizca gercek ve
cozulemeyen tasarim celiskisinde karar istenir (P3).

Kapsam:

- Backend bandi ayni backend'e ait testlerin basinda **bir kez**
  (design-spec §4, Detailed Results).
- Test kartlari bandin altinda ayni miktarda iceri alinir.
- Header duzeni her testte `STATUS | TEST ID + TEST NAME | DURATION`.
- Status rengi sol kenar, status metni ve gerekli vurgu alanlarinda ortak kural.
- Kart genisligi, border, tipografi, header yuksekligi, section bosluklari
  ortak.
- Grafik, tablo, metric summary ve test aciklamalari **teste ozgu kalir**.
- Status metni yaninda nokta/ikon yok; `SKIPPED` gorunur metni `SKIP`
  (design-spec §4).
- `RESULT` alani yalnizca WARN/FAIL/SKIP kartlarinda
  (design-spec §4).
- Anchor: `result-<backend>-<test_id>` (`test_id` tam slug, kamera yok).

### 3.2. Duration formatter

C++ rapor ureticisi ile TypeScript web UI **fiziksel olarak ayni helper'i
kullanamaz**. Sozlesme buna gore tanimlanir:

- Tek **kanonik formatlama sozlesmesi** ve **ortak test vektorleri** bulunur.
- C++ ve TypeScript tarafinda dil-yerel formatter bulunabilir.
- Iki uygulama ayni **boundary orneklerinde birebir ayni** cikti uretmelidir.
- `duration_ms` wire ve artifact verisinde **ham sayi** olarak korunur.

Ortak vektorler tek bir yerde yazilir ve iki tarafin testi de ondan beslenir;
boylece "ayni kural" bir iddia degil, olculen bir davranis olur.

### 3.3. Export PDF, Export JSON, Export Markdown

**Export PDF.** Nihai onayli etiket **`Export PDF`**'tir (design-spec §1) ve onayli
preview'lerde bu yazim kullanilir.

Kaynak koddaki etiket su an `Export as PDF` — 2.2'den kalan gecici bir durum. **Bu
maddede** kaynak etiket ile `tests/report_formats_test.cpp` icindeki assertion
**birlikte** `Export PDF` yapilir; o assertion bugun karakterizasyon olarak
isaretli.

`window.print()` cagirir. Uygulama **running footer uretmez** (review-plan
S6.9). Sayfa geometrisi ise onaylı print CSS'inin parcasidir: review-plan S6.8
`@page { size: A4; margin: 12mm 10mm 14mm; }` tanimlar ve marjlarin kullanicinin
print diyalogundan degistirilebilecegini belirtir. Bkz. 3.7.5.
`<title>` = hedef dosya adinin uzantisiz hali; gorunur H1 `V4L2 Camera
Diagnostic Report` olarak kalir. Nihai ad, `.pdf` uzantisi ve print secenekleri
browser'in kontrolunde.

**Export JSON / Export Markdown.** 2.10 her run'in ucunu de yazmasini sagladi,
dolayisiyla bu iki buton **var olan dosyalara goreli link** verir:

- Tarayicida **yeniden cikti uretmez** (no `Blob`, no client-side serialise).
- Dosya adini **turetmez**; 3.5'teki kuralla yazilmis gercek adi kullanir.
- Goreli oldugu icin rapor klasoru arsivlenip `file://` ile acildiginda da
  calisir.

**Preview.** Dort butonlu toolbar Faz 1'de onaylandi (yukarida §1.7 arsivi) (`TASARIM ONAYLANDI`,
2026-08-04). Bu madde o onayli tasarimi kaynak renderer'a aktarir; icerik ve dis
sablon degistirilmez.

### 3.4. Export DMESG

**Karar (2026-08-07):** DMESG artik JSON ve Markdown gibi **testler
tamamlandiktan sonra otomatik uretilen bir artifact**'tir. Sunucu bagimli
canli indirme modeli **kaldirildi**.

- DMESG diger artifact'lerle ayni anda uretilir (2.10, zorunlu artifact seti).
- Export butonu onceden uretilmis dosyaya **goreli download linki** verir.
- `file://` icin ozel disabled davranis **yoktur** — dosya zaten vardir.
  `render_dmesg_note_and_script()` mekanizmasi kaldirilir.
- Ad 3.5.1'deki tek uretim noktasindan gelir (`_dmesg.log`).
- Artifact uretilemediyse (yetki eksikligi vb.) link raporda **yer almaz**.
- Print gorunumunde export alani gosterilmez.

**Guvenlik siniri (degismedi):** `journalctl -k -b --no-pager` uretim
sirasinda **sunucu tarafinda** calisir; sunucu ayricaliksiz kalir,
`adm`/`systemd-journal` modeli korunur. Shell'e kullanici girdisi
**eklenmez**, `sudo journalctl` icin yeni privileged kod veya shell yolu
**eklenmez**.

Servis kullanicisina `adm` veya `systemd-journal` yetkisi verilir (installer
zaten bunu soruyor).

#### 3.4 uygulama sonucu (2026-08-08)

- `write_reports()` kernel log'u **HTML'den once** yazar, cunku HTML link
  uretip uretmeyecegini bilmek zorundadir. Zorunlu artifact **degildir**:
  `adm` uyeligi olmayan bir makine yine tam rapor uretir.
- `render_dmesg_action()` linki **yalnizca dosya gercekten yazildiysa** uretir.
  Aksi halde hicbir kontrol gosterilmez — yoktan bir href, tiklayinca bulunan
  bir 404'tur ve bu, yerini aldigi disabled butondan daha kotudur.
- Disabled buton, `export-dmesg-note` ve durum cozen script kaldirildi.
- Guvenlik siniri korundu: sabit komut (`journalctl -k -b --no-pager`),
  kullanici girdisi shell'e girmez, `sudo` yok, istemci dosya adi belirlemez.

Olculdu: gercek kosumda 154 KB `_dmesg.log` uretildi ve link ona isaret etti;
`journalctl` basarisiz kilindiginda ne dosya ne link ne buton uretildi, uc
zorunlu artifact etkilenmedi. Negatif yol sabotajla kanitlandi.

**Kapsam disi bulgu (P4):** `GET /api/dmesg` endpoint'i artik hicbir yerden
cagrilmiyor — olu kod. Kaldirilmasi §5.4'un konusudur, burada dokunulmadi.

### 3.5. Dosya adi sablonu

HTML, JSON ve Markdown **ayni kanonik tabani** paylasir; yalnizca uzanti degisir.
Uc artifact her run'da uretildigi icin (2.10) tabanin tek yerde uretilmesi
zorunludur.

#### 3.5.1. Tek uretim noktasi

HTML, JSON, Markdown, `<title>`, Export linkleri **ve** DMESG adi ayni
`canonical_report_basename()` sonucundan turer. Ayri string birlestirme kopyasi
olusturulmaz.

Testle kilitlenen ornekler: free-run + default; hardware-trigger + anvil +
stress-test; software-trigger; uzantili config filename; path iceren filename
girdisinin yalniz basename'e indirgenmesi; bos/uygunsuz filename icin guvenli
fallback.

#### 3.5.2. Gercek config kimligi

`<trigger_profile>` ve `<test_configuration>` degerleri profile/config **ID'sinden
tahmin edilmez**. Kullanici karari **gercek dosyanin uzantisiz adidir**.

- Run modeli, artifact isimlendirmesi icin secilen **Trigger Profile kaynak dosya
  adini** ve **Test Configuration kaynak dosya adini** acikca tasir.
- Free-run'da Trigger Profile parcasi **tamamen atlanir**.
- Test Configuration secilmemisse `default` kullanilir.
- Yol, uzanti ve path separator dosya adina **girmez**.
- Ayni ID'ye sahip fakat farkli kaynak dosya senaryosu yanlis ad **uretmemelidir**.

Mevcut `threshold_config_id` bu sozlesmeyi gercekten garanti ediyorsa **testle
kanitlanir**; garanti etmiyorsa ayri acik alan eklenir. Sessiz tahmin yapilmaz.

```text
<start_date>_<trigger_mode>_[trigger_profile]_<test_configuration>_v4l2_camera_diagnostic.html
<start_date>_<trigger_mode>_[trigger_profile]_<test_configuration>_v4l2_camera_diagnostic.json
<start_date>_<trigger_mode>_[trigger_profile]_<test_configuration>_v4l2_camera_diagnostic.md
<start_date>_<trigger_mode>_[trigger_profile]_<test_configuration>_dmesg.log
```

| parca | kural |
| --- | --- |
| `<start_date>` | run baslangic zamani, `YYYY-MM-DD_HH-mm-ss` |
| `<trigger_mode>` | `free-run`, `hardware-trigger` veya `software-trigger` |
| `[trigger_profile]` | Trigger Profile dosyasinin uzantisiz adi; free-run'da **tamamen atlanir** |
| `<test_configuration>` | Test Configuration dosyasinin uzantisiz adi; secilmemisse `default` |

- Dosya yolu ve config uzantisi dosya adina **girmez**.
- HTML `<title>`, `v4l2_camera_diagnostic` dahil kanonik tabanin **uzantisiz**
  hali olur. Browser print sirasinda onerilen PDF adi bu title uzerinden olusur.
- Export JSON ve Export Markdown adi **sonradan uretmez**; bu kuralla yazilmis
  gercek dosyalara baglanir (3.3).

Free-run ornegi:

```text
2026-07-30_12-02-38_free-run_default_v4l2_camera_diagnostic.html
2026-07-30_12-02-38_free-run_default_v4l2_camera_diagnostic.json
2026-07-30_12-02-38_free-run_default_v4l2_camera_diagnostic.md
2026-07-30_12-02-38_free-run_default_dmesg.log
```

Hardware-trigger ornegi:

```text
2026-07-30_12-02-38_hardware-trigger_anvil_stress-test_v4l2_camera_diagnostic.html
2026-07-30_12-02-38_hardware-trigger_anvil_stress-test_v4l2_camera_diagnostic.json
2026-07-30_12-02-38_hardware-trigger_anvil_stress-test_v4l2_camera_diagnostic.md
2026-07-30_12-02-38_hardware-trigger_anvil_stress-test_dmesg.log
```

### 3.6. Rapor ici duzeltmeler

- Free-run'da `Trigger Profile: Not required (free-run)`.
- DMABUF: band `BACKEND DMABUF`, yontem aciklamasi
  `MMAP buffers exported with VIDIOC_EXPBUF`, JSON anahtari `dmabuf` kalir.
- Gorunur etiketler 4.0'daki ortak terminolojiye uyar: distribution count
  `Warnings` → `Warned` (design-spec §2), test status `SKIPPED` →
  `SKIP`. Log severity `warn` **degismez**. Ic anahtarlar, veri modeli ve JSON
  degismez.

### 3.7. Tasarim belgesi sapma envanteri — TAMAMLANDI (arsiv)

Durum: `UYGULANDI`. Uc inceleme turu boyunca `report-ui-design-spec.md`
bolumleri (S1-S6) kaynak render'a karsi denetlendi, sapmalar duzeltildi.
Sureclerin ayrintili kaydi kaldirildi; **Faz 3b** bu isin yerine gecti ve
sozlesmeyi testle kilitledi (`report_card_contract`, `metric_name_contract`).

O turlarda cikan ve hala gecerli olan tek acik madde:

#### 3.7.7. S6.6 continuation header — KAPATILDI (2026-08-08)

Olculdu ve **karar bekleyen bir celiski olmadigi** anlasildi: Chrome 146'da
`position: running()` ve `string-set` desteklenmiyor, `display:
table-header-group` bir `div` uzerinde ise yaramiyor (iki sayfalik kartta
baslik bir kez cizildi). Calisan tek yol gercek `<table>` + `<thead>` ve o da
"tablolar div+grid ile kurulur" hukmunu bozar.

Ayrinti ve olcum tablosu: `docs/report-ui-design-spec.md` §6.6.

## Faz 3b — Section/grafik sozlesmesini renderer'a uygula

Durum: `UYGULANDI` (2026-08-08) — DOM sozlesmesi testle kilitli, production
HTML uzerinde olculdu. `DOGRULANDI`'ya yukseltilmesi kullanicinin gercek cihaz
kosumunu paylasmasina baglidir (Kural 10, ucuncu seviye).

Sutun imzasi uyumu: **58 → 1** (2026-08-08). Kalan tek fark imza degil
**veri**dir: T13'un sinir tablosu prob noktasini basliginda tasir
(`Below cliff · 90 ms` vs preview'deki `94 ms`) ve fixture ile preview farkli
`first_miss` degeri kaydeder. Sekil sozlesmedir, icindeki sayi veridir.

Bu turda ayrica kapatildi:

- `Test Configuration` bes teste daha eklendi (T12/T14/T15/T16/T17); toplam
  24/24 olcum karti tasiyor.
- T14'un **sequence diyagrami** kaldirildi (S5 yasagi): uc kutulu
  TRIGGER EDGE → PIPELINE → DQBUF seridi olcum tasimiyordu, dort latency
  istatistigi yerini aldi.
- T18'de bes satirlik uydurma `Initial control snapshot` bloku kaldirildi;
  `Access` sutunu artik run'dan **okunuyor**, varsayilmiyor.
- T19'un `Coverage` sutunundaki sabit `"20/20"` kaldirildi; sutunlar onaylı
  imzaya (`Pixel format`, `Throughput`) cekildi.

### 3b.5. Metrik adi sozlesmesi — UYGULANDI (2026-08-08)

Fixture/runner boslugu backend'de olculdu ve **bosluk degil, ad uyusmazligi**
oldugu bulundu. Bu, bu fazin en onemli bulgusudur.

Olcum: `docs/assets/source-render/fixtures.py` 133 metrik adi tasiyor,
`diagnostic_runner.cpp` 150 ad uretiyor, **ortak olan 11**. Yani fixture,
runner'in adlarini degil renderer'in **tahminlerini** tasiyordu. Her rapor
fixture'dan tam gorunuyor, ayni renderer gercek bir kosumda sutunu bosaltiyordu.

| katman | T15 "max" icin ad |
| --- | --- |
| runner uretir | `nonblock_latency_max` |
| renderer arardi | `nonblock_max_ms` |
| fixture tasirdi | (hic yok) |

Uc katman uc farkli ad kullaniyordu. Runner'in **her istenen metrigi urettigi**
dogrulandi: T14 6/6, T15 10/10, T24 4/4.

Uygulama: renderer 109 metrik aramasinin tamami runner adlarina baglandi; eski
adlar `value_of_any` zincirinde **yedek** olarak birakildi, boylece eski adla
kaydedilmis bir run da okunmaya devam eder.

Koruma: `tests/metric_name_contract_test.cpp` (CTest `metric_name_contract`).
Iki kaynak dosyayi statik tarar — bir fixture, modellemek icin var oldugu
kusuru yakalayamaz. Calisma zamaninda uretilen adlar (`hits_5ms`,
`res_1920x1080_mean`, `<format>_latency_mean`) onek listesiyle taninir.
Sabotajla kanitlandi: `latency_stddev` → `latency_stddev_ms` cevrildiginde
build exit 0, test yakaladi.

**Kalan `Unavailable` satirlari fixture bosluğudur, renderer kusuru degil.**
Olculdu: T12/T14/T15/T17/T18/T20/T21'in fixture'inda ne yapilandirma detay
satiri ne de parametre metrigi var; T14'un `latency_min_ms` ve T15'in
`*_max_ms`/`*_min_ms`/`*_stddev_ms` metrikleri hic kaydedilmiyor. Renderer
dogru davraniyor: uydurmuyor, yoklugu bildiriyor. Kapatmak icin once gercek
runner'in bu metrikleri kaydedip kaydetmedigi olculmelidir — fixture'i hedefe
gore doldurmak renderer'i kanitlamaz.

Girdi: `docs/report-ui-design-spec.md` §"Section ve Grafik Sozlesmesi"
(S1-S10) ve `docs/assets/refactored_previews/t01..t26-preview.html`
(26 dosya, tasarim onaylandi 2026-08-07).

**Onemli:** preview'lerin onaylanmasi implementation degildir (CLAUDE.md
Kural 4). Bu faz kapanmadan sozlesme uygulanmis sayilmaz.

### 3b.1. Renderer sapma envanteri

Durum: **OLCULDU** (2026-08-08). Yontem: `generate.py` → `render_fixtures.cpp`
→ gercek `write_reports()` cikisi, 3 mod. Olculen dosya free-run raporu.

| olcut | uretilen | hedef | not |
| --- | --- | --- | --- |
| `<table>` | **41** | **0** | tablolar `div`+grid olmali |
| `item-label` | **0** | 22 dosyada | S2 |
| `has-items` merdiven | **0** | 22 dosyada | S3 |
| `Measurement` / `Measurement Result` | **0** | 22 testte | S1 |
| ayri section adi | 81, hicbiri hedefle ayni | 7 kanonik ad | S1 |
| `Type` sutunu | **yok** | her olcum tablosunda | S1 |
| `Outcome` / `State` sutunu | **var** (T04, T05, T09, T10, T19-T22) | yasak | `Detail`+`Status` |
| `Metric definitions` tablosu | **8 testte** | **0** | design-spec |
| `Recorded values` ham dump | **T01** | **0** | design-spec |
| SVG | 16 (T03,T06,T07,T09,T13,T16,T17,T23,T24,T25,T26) | 13, farkli testlerde | S5, S8 |
| SVG viewBox | `~520` birim | render genisligi (906/938) | S8, olcek 1.0 |

Sonuc: bu bir ayar farki degil, **mimari fark**. Mevcut renderer generic —
metrikleri sezgisel gruplayip (`group_metrics`, `is_chartable_metric`,
`sweep_prefix`) otomatik tablo/grafik uretiyor. Hedef ise teste ozgu **acik**
duzen. Uygulama 3b.2'deki bildirimsel katmanla yapilir.

### 3b.2. Uygulama — UYGULANDI (2026-08-08)

Sozlesme testi: `tests/report_card_contract_test.cpp` (CTest `report_card_contract`),
26 testin tamamini kapsayan fixture. Once **88 sapma ile kirmiziydi**, simdi 0.

| olcut | once | sonra |
| --- | --- | --- |
| kart icinde `<table>` | 41 | 0 |
| kanonik olmayan section adi | 81 | 0 |
| `item-label` / `has-items` | 0 | tum ilgili testlerde |
| `Outcome` / `State` sutunu | 8 testte | 0 |
| `Metric definitions` sozlugu | 8 testte | 0 |
| `Recorded values` ham dokumu | T01 | 0 |
| `Measurement Result` tasiyan kart | 3 | 24 (T01/T02 envanter, muaf) |
| verdict satiri `Unavailable` | 58/64 | **0/64** |
| SVG olcek (canli Chrome) | 0.446 - 1.035 | **1.0000, 16/16** |
| grafik kapsayicisi tasmasi | 6 | 0 |

Uygulama sirasinda **kendi degisikliklerimin** urettigi dort kusur olculdu ve
duzeltildi; hicbiri kaynak koda bakarak gorunmuyordu:

1. `split_unit` binlik ayiracini birim sanip `4,915,200` degerini `4`'e indirdi.
2. `"4,915,200 bytes / 4.69 MiB"` bilesik degerinde MiB okumasi yutuldu.
3. `item-label` dort testte `<section>` disinda kaldi (girintisiz, kutusuz).
4. `section_close()` iki testte `if` blogunun icindeydi: veri yoksa
   `Test Configuration`, `Measurement`'in **icine** yuvalandi.

3 ve 4 icin sozlesme testine yapi kontrolu eklendi ve sabotajla kanit alindi
(`54 open, 53 closed`, build exit 0).

### 3b.2b. Eski davranisi kilitleyen testler

Uc test kirildi, hicbiri zayiflatilmadi:

- `report_writer`: `<table class="evidence">` → `class="grid-head cols-`;
  `N/A` iddiasi kaldirilan dokumu okuyordu, sentinel korumasi `-500`
  gorunmemesiyle olculuyor.
- `result_card_template`: `Result` **section**'i → `result-{status}` div.
- `test_content_registry`: onaylı sutun listesi data-contract'a cekildi;
  `Metric definitions` iddiasi **tersine cevrildi** (geri gelmesi regresyon).

### 3b.2c. Cozulen celiski (P3)

"Grafik yatay kaymamali" (eski) ile S8 "olcek 1.0" (yeni) carpisti. Olcum
gosterdi ki kart govdesi 968px veriyor ve grafik 906px — **sigiyor**, yani
`overflow-x` gerekmiyor. Her iki karar da korundu.

### 3b.2 (eski madde) Uygulama

Her test icin **once** DOM sozlesmesi testi yazilir (test-first), sonra
renderer degistirilir. Teste ozgu renderer'lar ortak dis kabuk icinde
kalir (Kural 5).

### 3b.3. Icerik uyumu — KISMEN UYGULANDI (2026-08-08)

Uretilen HTML `docs/renderer-data-contract.md` ile karsilastirildi.

| olcut | once | sonra |
| --- | --- | --- |
| eksik sutun imzasi | 58 | **19** |
| `Test Configuration` yok | 6 test | 2 (T01/T02 probe, muaf) |
| `Test Configuration` `<dl class="kv">` olarak | 12 test | 0 (hepsi 5-sutun grid) |
| tekrar eden `item-label` | 2 test | 0 |
| eksik `Aggregate` tablosu | 16 test | 0 |
| **uydurma olcum sabiti** | 18 satir | **0** |

**En onemli bulgu — uydurma olcumler.** Alti renderer, veri yokken raporda
gercek olcum gibi gorunen sabitler basiyordu:

- T18: `HDR enable = 0 | 20/20 | 44.801 ms | APPLIED` (bes satir)
- T24: `Mean | 76.476ms | 82.888ms | +6.412ms` (alti satir)
- T25: `/dev/video0 | master | 50 / 50 | 44.801 ms` — run'in hic acmadigi cihaz
- T19: `1920x1280 | 20/20 | 44.805 ms`
- T23: `Full run summary` uc sutunu `.empty()` fallback'i ile `82ms`/`21ms`

Hepsi kaldirildi; veri yoksa tablo yoklugu **bildiriyor**. Bir tanisi sabotajla
kanitlandi (`76.476` geri konunca test yakaladi, build exit 0).

Ayrica **olu fallback zinciri** bulundu: `value_of()` hicbir zaman bos string
donmez (eksik metrik `"Unavailable"` doner), dolayisiyla
`value_of(...).empty() ? "82ms" : ...` kaliplarinin tamami olu koddu ve
tablolar `Unavailable` basiyordu.

**Kapanmayan 19 imza** ve 6 `Unavailable` satiri: T18/T20/T21'in fixture'i
yapilandirma parametresi kaydetmiyor, ve bazi teste ozgu kanit tablolari
(`.pw`, `.win`, `.ev`, `.cev`) hedefteki sutun adlarini henuz tasimiyor.
Bunlar 3b.4'te kapanir.

### 3b.4. Dogrulama — UYGULANDI (2026-08-08)

**Ajan tarafinda iki seviye** (Kural 10):

1. **DOM sozlesmesi** — `tests/report_card_contract_test.cpp`, CTest
   `report_card_contract`. 26 testin tamamini kapsayan fixture. Kilitledigi
   sozlesme: S1 (div+grid, kanonik section adlari, `Status`ta `—` yok, yasak
   sutun yok, her olcum karti `Measurement Result` + `Test Configuration`
   tasir, verdict satiri `Unavailable` okumaz), S2/S3 (item basligi, merdiven,
   section dengesi, bolum disina kacan item yok), S4 (`Unit` sozlugu),
   S8 (viewBox genisligi ve render genisliginin sabitlenmesi), ve
   uydurma olcum sabiti yasagi.
2. **Production HTML** — `write_reports()` ciktisi uretilip olculur.

Dort guard gecici geri alma ile yuk tasidigini kanitladi; her sabotajda build
exit code ayrica olculdu (Kural 9):

| sabotaj | yakalayan kural |
| --- | --- |
| `section_close()` kosullu bloga alindi | `54 open, 53 closed` |
| grafik genisligi `width:100%` yapildi | genislik sabitlemesi kayboldu |
| `76.476ms` uydurma satiri geri konuldu | uydurma olcum sabiti |
| eksik metrik adi (spec yanlisligi) | verdict satiri `Unavailable` |

**Ucuncu seviye kullanicidadir.** Ajan PNG/screenshot/PDF **uretmez**
(kullanici karari, 2026-08-08). Gorsel dogrulama, kullanicinin gercek cihazda
aldigi kosum sonucuyla yapilir: kullanici raporu paylasir, kontrol o zaman
yapilir. Bu gerceklesene kadar madde `UYGULANDI` kalir, `DOGRULANDI` olmaz.

`docs/assets/source-render/` su an arac dizinidir ve hedefi eski
`docs/assets/previews` setidir; `refresh.sh`/`manifest_check.py` kapisi bu
fazda **kullanilmadi** (bkz. o dizinin README'si).

---

## Faz 4 — Web UI — TAMAMLANDI (arsiv)

Durum: `UYGULANDI` (2026-08-04). 4.0-4.8'in tamami kontrol listesinde isaretli.

| madde | kapsam |
| --- | --- |
| 4.0 | Ortak status terminolojisi (`PASS`/`WARN`/`FAIL`/`SKIP`) + `Trigger Profile` etiketi |
| 4.1 | Dashboard — Pass Rate 2.8'deki ortak yardimciyi kullanir, kendi hesabini yazmaz |
| 4.2 | Cameras |
| 4.3 | Trigger Profile (eski Profiles) |
| 4.4 | Test Selection secim modeli |
| 4.5 | Test Configuration + v3→v4 migrasyon UI'i |
| 4.6 | Report Formats sayfasi ve navigation adimi **kaldirildi** (2.10) |
| 4.7 | Live Output / Result Output |
| 4.8 | Config varsayilanlarinin ekranlari doldurmasi |

## Faz 5 — QA altyapisi

Durum: `UYGULANDI` (5.1-5.6). Iki olcum bulgusu acik kaldi: **5.4 envanteri**
ve **5.7**.

| madde | kapsam | durum |
| --- | --- | --- |
| 5.1 | Vitest + RTL, CI entegrasyonu | uygulandi |
| 5.1b | 2.6 ve 2.10 icin zorunlu testler (restart, legacy JSON, export linkleri, dosya adi) | uygulandi |
| 5.2 | CSS token dogrulamasi — cozulmeyen `var()` referansi CI'da yakalanir | uygulandi |
| 5.3 | Rapor QA'i — 3 mod x 4 viewport olculdu | uygulandi (asagida) |
| 5.4 | Olu kod temizligi | uygulandi (asagida) |
| 5.5 | Erisilebilirlik | uygulandi |
| 5.6 | Belge duzeltmeleri | uygulandi |

### 5.3. Rapor QA'i — uygulama sonucu (2026-08-08)

Ajan tarafinda **HTML DOM kontrolleri** kalir. Screenshot ve print-gorunumu
karsilastirmasi ajan tarafindan **yapilmaz** (kullanici karari, 2026-08-08);
gorsel QA kullanicinin gercek cihazda aldigi kosum sonucuyla yurutulur.

Olcum: uc trigger modu x dort viewport (1440/1024/768/390) = **12 kombinasyon**,
canli Chrome. Dort kosulun (design-spec §6) her biri 12/12 gecti: document yatay scroll
yok, grafik/legend kirpilmiyor, test-header hucreleri cakismiyor, uzun
identifier parent'i buyutmuyor.

Iki bulgu:

- **Gercek kusur:** 1024px altinda alti grafik kirpiliyordu. Sabit 906px
  genislik (design-spec S8) dar ekranda sigmiyordu. Cozum kucultmek **degil** —
  kucultmek her etiketi de olceklerdi, yani sabit genisligin var olma sebebini
  yok ederdi. Grafik artik kendi kutusunda kayiyor; sayfa yatay kaymiyor.
- **Olcum hatasi:** 390px'te 52 "header cakismasi" raporlandi. 700px altinda
  header tek sutuna dusuyor ve ogeler alt alta diziliyor; kontrol "sag kenar >
  sonrakinin sol kenari" bakiyordu, ki dikey yigilmada bu her zaman dogrudur.
  Dikdortgen kesisimine cevrilince cakisma **0** cikti.

Celiski cozuldu (P3): `report_writer_test` `overflow-x: auto`'yu tumden
yasakliyordu. Kaygi dogruydu ama iddia fazla genisti. Ayrim netlestirildi:
**document** yatay kaymaz, **grafik kutusu** kayabilir. Ikisi de testte ayri
ayri kilitli, sabotajla kanitlandi.

### 5.4. Olu kod temizligi — UYGULANDI (2026-08-08)

**Kaldirildi:**

- `GET /api/dmesg` + `read_kernel_log()` + `dmesg_download_filename()` +
  `Content-Disposition` blogu + `WebServerOptions::kernel_log_reader` test
  seam'i + `download`/`run` query parametrelerinin okunmasi.
  Bunlarla birlikte **on guvenlik assertion'i** da gitti (bilinmeyen run reddi,
  path traversal, gevsek eslesme, istemci filename'i, CRLF injection). Artik
  var olmayan bir yolu koruyorlardi; yerlerine endpoint'in **404 dondugunu** ve
  404'un icinde kernel log veya download header **bulunmadigini** olcen bir test
  kondu. Sabotajla kanitlandi.
- `POST /api/profiles/validate` — `POST /api/profiles` ayni kod yolunu
  paylasiyor, yani hicbir dogrulama kaybolmadi.
- `styles.css`'te **kopya blok** (`.results-table` ailesi iki kez tanimliydi;
  ilk kopya `status-skip` kuralini tasimiyordu, o kaldirildi).
- `.clickable-row:hover` — (0,1,0) ozgulukte, `.results-table tr:hover`
  (0,1,1) tarafindan **her zaman** eziliyordu. Uygulanamayan bir kural,
  verilmis ama sessizce yururlukte olmayan bir karar gibi okunur.
- Dokuz olu CSS blogu: `channel-id-label`, `chip-core`, `chip-exp`,
  `filter-toggles`, `inline-checkbox`, `layout-horizontal`, `output-controls`,
  `switch-thumb`, `switch-track`, `toggle-switch-pill`.

**Kaldirilmadi, gerekcesiyle:**

- `GET /api/health` — bir health endpoint'i tanimi geregi uygulamanin kendisi
  tarafindan cagrilmaz; monitoring, systemd veya load balancer cagirir.
  "Bu depoda cagiran yok" onun kullanilmadiginin kaniti **degildir**.
  `docs/architecture.md` bunu artik acikca soyluyor.
- Plan envanterindeki `status-badge`, `test-selection-page`,
  `test-category-group`, `chip-default`, `config-toolbar-panel` — olculdu:
  bunlar TSX'te **kullaniliyor**, CSS'leri yok. Yani olu kod degil, stilsiz
  render eden veya test kancasi olan siniflar; silmek yanlis olurdu.
- `tone-*`, `chip-*`, `status-*` aileleri — calisma zamaninda uretiliyorlar
  (`tone-${tone}`, `chip ${def.class}`). Statik tarama onlari goremiyor;
  silseydim calisan stili kirardim.

**Kapsam disi (P4):** `ProfileSelectionPage.tsx:127` `onAssignmentModeChange`
kullanilmayan parametre uyarisi veriyor. Dokunmadigim bir dosya, uyari
onceden vardi.

### 5.7. T26 warm-up olcumu — UYGULANDI (2026-08-08)

**Sorun neydi.** Stabilize olmayan bir cycle'da `warmup_frame` hic atanmiyor,
baslangic degeri `MAX_FRAMES_PER_CYCLE` (30) olarak kaliyordu. Rapor bu yuzden
**parametrenin kendisini olculmus sonuc gibi** gosteriyordu — gercek deger 31
de olabilirdi 500 de. Dahasi bu 30, ortalamaya ve verdict'e giriyordu.

**Uygulama.**

- Stabilite analizi `find_warmup_frame()` olarak `stats.hpp`'ye tasindi: saf
  fonksiyon, donanimsiz test edilebilir. `WarmupResult` ya bir olcum tasir ya da
  **neden olculmedigini** (`NeverSettled` / `NoReference` / `TooFewFrames`).
- Steady-state referansi artik kendi de denetleniyor: tail'in stddev'i tolerans
  disindaysa referans **uretilmiyor**. Referansi yargiladigi pencerenin
  icinden almak, "hic oturmadi" ile "oturdu ama tail gurultuluydu" durumlarini
  ayirt edilemez kiliyordu.
- Censored cycle **ortalamaya girmiyor**. Yeni metric: `censored_cycles`.
- Verdict: censored cycle varsa `PASS` → `WARN`, ve ozet maksimumun bir
  **alt sinir** oldugunu soyluyor.
- Rapor: `Cycles not measured` satiri eklendi; `Longest warm-up` →
  `Longest measured warm-up` (`Measured cycles only`).

**Kanit.** `stats_test` bes vaka: hic oturmayan, oturan (dogru frame),
referanssiz, kacan frame'li, cok kisa pencere. Sabotajla dogrulandi — "hic
oturmadi" dalini `stabilized = true` yapinca build exit 0, test yakaladi.

## 6. Kalan is

Faz 0-5'in tamami uygulandi ve kontrol listesinde acik madde kalmadi.

**Tek acik is: cihaz kosumu dogrulamasi.** Kural 10'un ucuncu seviyesi —
kullanici gercek donanimda kosup raporu paylasacak. O zamana kadar Faz 3b
`UYGULANDI` kalir, `DOGRULANDI` olmaz.

## 7. Kontrol listesi

> Bu liste kaynak kod uygulamasini izler. Tasarim dogrulamasi
> `docs/report-ui-design-spec.md` kontrol listelerindedir.

Tamamlanan maddeler faz ozetine indirildi; tek tek listelenmesi belgeyi
uzatiyordu ve her biri kendi faz bolumunde zaten kayitli.

| faz | isaretli madde |
| --- | --- |
| 0 | 9 |
| 1 | 8 |
| 2 | 15 |
| 3 | 6 |
| 3b | 5 |
| 4 | 10 |
| 5 | 6 |

Acik:

- [x] 5.4 Olu kod temizligi (2 endpoint + kopya CSS blogu + 9 olu sinif)
