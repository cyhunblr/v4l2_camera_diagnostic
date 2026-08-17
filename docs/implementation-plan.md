# Uygulama Plani — Web UI Denetimi ve Rapor Kararlari

Bu belge, web arayuzunun menu menu yapilan derinlemesine denetiminde bulunan
sorunlari ve `docs/report-ui-design-spec.md` ile sonuclandirilan rapor
kararlarini tek bir uygulama sirasinda birlestirir.

Kapsam: SPA (`source/frontend`), web sunucusu (`source/backend/web`), rapor
ureticisi (`source/backend/core/report_writer.cpp`), test/threshold registry'leri
ve bunlarin dokundugu hw davranislari.

Bu belge **ne yapilacak** sorusunu yanitlar. Tasarim otoritesi
`docs/report-ui-design-spec.md`, veri sozlesmesi ise sozlesme testleridir
(`test_configuration_contract`, `report_card_contract`, `test_content_registry`,
`metric_name_contract`). Onay araci olan 26 preview HTML ve
`renderer-data-contract.md` 2026-08-12'de kullanici karariyla kaldirildi;
bu belge onlari uygulamaya cevirir ve onlarla **celisemez**. Bir tasarim kurali buraya kopyalanmaz, atif verilir.

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

Testle kilitlenen ornekler: free-run + default; hardware-trigger + bench-rig +
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
2026-07-30_12-02-38_hardware-trigger_bench-rig_stress-test_v4l2_camera_diagnostic.html
2026-07-30_12-02-38_hardware-trigger_bench-rig_stress-test_v4l2_camera_diagnostic.json
2026-07-30_12-02-38_hardware-trigger_bench-rig_stress-test_v4l2_camera_diagnostic.md
2026-07-30_12-02-38_hardware-trigger_bench-rig_stress-test_dmesg.log
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

Olcum (o tarihte, dizin 2026-08-12'de kaldirildi): `source-render/fixtures.py`
133 metrik adi tasiyor,
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
(S1-S10) ve 26 onayli preview HTML (tasarim onaylandi 2026-08-07; dosyalar
onay tamamlandiktan sonra 2026-08-12'de kaldirildi).

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

Uretilen HTML o tarihte `docs/renderer-data-contract.md` ile karsilastirildi
(belge 2026-08-12'de kaldirildi; ayni sozlesme artik onayli preview'lerde).

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

`source-render/` bir arac diziniydi ve `refresh.sh`/`manifest_check.py` kapisi bu
fazda **kullanilmadi**; dizin 2026-08-12'de kaldirildi.

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

## Faz 6 — Cihaz kosumu bulgulari: stilsiz sinif ailesi ve SKIP govdesi

Durum: `ACIK`. Kaynak: kullanicinin 2026-08-09 cihaz kosumu
(`hardware-trigger_bench-rig_default`, run `1786271499-87889`) ve o rapor uzerinde
yapilan olcum. Kural 10'un ucuncu seviyesi ilk kez veri uretti.

### 6.1. Decision Lock (P1)

```text
DECISION LOCK
- Degistirilecek:
  (a) report_writer.cpp CSS: markup'ta kullanilan ama kurali OLMAYAN 12 sinif
      icin kural eklenir — result-warn / result-fail / result-skip,
      T03 ailesi (stacked-chart, cycle-row, cycle-label, cycle-info,
      bar-streamon, bar-firstframe, legend-streamon, legend-ff),
      T06 ailesi (rel-chart, rel-row, rel-label, rel-info,
      bar-pass, bar-warn, bar-fail), ortak scale-name.
  (b) SKIP statulu kart govde uretmez; yalnizca result-skip banner'i tasir.
- Aynen korunacak: mevcut bar-row/bar-label/bar-track/bar-fill ailesi (T17 vb.
  dogru render ediyor); test id'leri; dosya slug'lari; JSON/MD ciktisi;
  legend'in inline renkleri (#c55757 / #b8860b / #4b9b69).
- Yerlesim sirasi: banner kart header'indan HEMEN sonra, section'lardan once.
- Zorunlu metinler: prefix statuye bagli — "Failed:" / "Warned:" / "Skipped:".
- Zorunlu palet (design-spec S "Result blogu", degistirilemez):
  result-fail #6e2424 / #fff7f7 · result-warn #6e4f00 / #fffdf5 ·
  result-skip #596776 / #f8f9fa · ortak padding:12px 16px; font-size:12px;
  font-weight:600.
- Yasaklanan / karari reddedilen alternatifler:
  * generic `result` sinifi kullanilmaz ve CSS'te tanimlanmaz (design-spec).
  * eksik kural "onemsiz" diye birakilmaz; kullanilan her result-* tanimli
    olmali VE tanimli her kural kullanilmis olmali (iki yonlu, design-spec).
  * SKIP kartina govde "zaten olculmus" diye birakilmaz.
  * preview'de olmayan renk/olcu uydurulmaz; degerler preview'den birebir.
- Onay gerektiren noktalar (uygulanmaz, raporlanir — P3):
  * T05 verdict siddeti: ayni kanit (0/3 recovery) preview'de FAIL, canlida WARN.
  * T19 WARN metni hicbir preview senaryosunda tasarlanmamis.
- Kaynak plan bolumleri: design-spec "Result blogu" ve S3 (merdiven);
  onayli preview'ler t03/t04/t06/t08/t12/t13/t14/t16/t25; plan §5.4
  (ayni hata kalibi: markup var, CSS yok → stilsiz render).
```

### 6.2. Olcum — neden bu bir kusur

`report_writer.cpp` uretilen stylesheet'te bu 12 sinifin **hicbiri icin kural
yok**; git gecmisi bunlarin hic eklenmedigini gosteriyor (regresyon degil,
hic transfer edilmemis). Sonuc canli raporda:

| bulgu | olcum |
| --- | --- |
| `result-warn` (T04/05/08/13/19), `result-skip` (T25) | ciplak `<div>` ile ayni computed style: seffaf zemin, `padding:0`, `16px/400` — tasarlanan amber/gri kutu yok |
| T03 `bar-streamon`/`bar-firstframe` | `<i>` ogesi kural almayinca `display:inline` kaliyor, inline `width:88.72%` **yok sayiliyor**; iki segment cokup etiketleri birlesiyor ve ekranda `1141145` okunuyor |
| T06 `bar-pass` | `width:100%` var ama zemin seffaf → dolgu gorunmez, yalnizca gri track kaliyor |
| T03/T06 satir ve etiket aileleri | grid kurali olmayinca tek satir yerine dikey yigiliyor ("Full cycles" / "100%" / "20/20" ayri satirlarda) |
| `scale-name` | eksen adi 9px ortali yerine 16px govde metni |

### 6.2b. Uygulama sonucu — 6.1(a), 6.3, 6.4

Durum: `UYGULANDI` (2026-08-09). DOM sozlesmesi
`tests/report_css_contract_test.cpp` ile kilitli ve production
`write_reports()` ciktisi uzerinde olculdu. `DOGRULANDI` **degil**: Kural 10'un
ucuncu seviyesi kullanicinin cihaz kosumunu paylasmasini bekliyor.

| madde | sonuc |
| --- | --- |
| 6.1(a) eksik CSS | 12 sinif + `.bar-warn` eklendi; `header-title-group` olu sinif olarak **kaldirildi** (hicbir kural, test veya frontend referansi yoktu) |
| 6.1(a2) `.bar-track` iki kabuk | Onaylı tasarim T03/T06 icin `display:flex`, T17/T11/T15/T19/T21 icin blok kullaniyor. Paylasilan kural degistirilmedi; flex varyanti `.stacked-chart .bar-track, .rel-chart .bar-track` ile kapsandi |
| 6.1(b) kopya banner | T13/T19/T25 iki banner basiyordu: dispatcher **ve** 14 teste ozgu renderer ayni blogu ekliyordu. 14 kopya kaldirildi, dispatcher tek kaynak |
| 6.3 SKIP govdesi | Kapi statuye degil **bosluga** bagli: `Skipped && metrics.empty() && details.empty()`. Kanit kaydetmis bir skipped test govdesini korur |
| 6.4 binlik ayirici | `display_number()` eklendi. `number()` **degistirilmedi**: 52 cagri yeri SVG koordinati ve CSS genisligi besliyor, virgul orada geometriyi bozar |

**6.4 ilk turda ayiriciyla sinirliydi** (kullanici karari, secenek C). Ondalik
hassasiyet sonraki turda ele alindi; asagidaki 6.9/6.10 gecerli durumdur.

Ayirici sorusu koddan cozuldu, tahminle degil: `grouped_bytes()` zaten vardi ve
tamsayi byte sayilarini grupluyordu (`5,439,744`), kesirli degerlerin ise hic
gruplama yolu yoktu. t22'nin `4096`'si ve t03'un `3000`'i preview'de de
ayirsizdir — degistirilmedi.

Sabotaj kaniti (her birinde build exit code ayrica olculdu, Kural 9):

| sabotaj | yakalayan |
| --- | --- |
| `.result-warn` kurali silindi | stilsiz sinif + palet assertion'i |
| palet degeri bozuldu | palet assertion'i (kural var, renk yanlis) |
| `bar-streamon` `display:inline` | inline-width assertion'i |
| kopya `result_block` geri kondu | kart basina en fazla 1 banner |
| SKIP kapisi `true` yapildi | SKIP govde assertion'i |
| SKIP kapisi yalniz statuye baglandi | `report_writer` ("capture capability is not presented") |
| `display_number` gruplamayi biraki | ayirici assertion'i |

Geometri guvenligi ayrica olculdu: uretilen HTML'de `style="width:"`,
`x=`, `y=`, `cx=`, `cy=`, `x1/x2/y1/y2`, `width=`, `height=`, `r=`
niteliklerinin **hicbirinde** virgul yok.

### 6.5. Tipografi — 6.5 ve 6.6

Durum: `UYGULANDI` (2026-08-09).

Onaylı kabuk o tarihte `refactored_previews/detailed-result-card.css`
dosyasiydi; preview'ler onu `<link>` ile cagirir ve uzerine kendi
`<style>`'lariyla ekleme yapardi. Karsilastirma **o dosyaya** karsi yapildi.
Kabuk artik `report_css_contract` testinde kilitli; preview seti 2026-08-12'de
kaldirildi.

Kart, header, `h2`, `status`, `duration`, `section` ve `item-label`
kurallari **birebir ayni** cikti. Iki gercek fark vardi:

| madde | onaylı | uretimdeki | sonuc |
| --- | --- | --- | --- |
| 6.5 `section-label` | 26 preview'in tamami kabuktaki duz etiketi ezip `border-bottom:2px solid #2d3a47` + `letter-spacing:.4px` + `color:#2d3a47` veriyor | yalnizca kabuk formu; alt cizgi **hic yok** | uygulandi |
| 6.6 font stack | `Inter, ui-sans-serif, system-ui, -apple-system, ...` | iki UI anahtar sozcugu **eksik** | uygulandi |

t01 ve t03 kuralin kisa formunu yazar; renk ve letter-spacing'i kabuktan
devralirlar ve ayni 2px cizgiyi boyarlar, yani render sonucu ayni. t03
**2026-08-09 02:52**'de guncellenmis (en yeni parti), yani kisa form sonradan
alinmis bir karar degil.

`item-label` kendi daha ince `1px #dfe5eb` cizgisini korudu (design-spec S2);
olcumle dogrulandi, section kurali ona sizmadi.

**Alinmayan (P4, raporlanir):** ayni kabuk dosyasinin `14px/1.45` taban
olcusu, `#17202b` metin ve `#e9edf2` zemin degerleri. Onlar tek kartlik
**preview sayfasini** tanimlar; uretimde `body` ayni zamanda rapor header'ini,
Overview tablosunu ve footer'i giydiriyor ve **52** kart kurali kendi
`font-size`'i olmadigi icin ondan devralir. Bu sayfa capinda bir yeniden
bicimlendirmedir, bu adimin sapmasi degil.

Sabotaj kaniti (build exit code ayrica olculdu):

| sabotaj | yakalayan |
| --- | --- |
| `section-label` alt cizgisi kaldirildi | 6.5 assertion'i |
| font stack eski haline dondu | 6.6 assertion'i |

6.6'da **ilk guard yuk tasimadi**: kuralin ustundeki gerekce yorumu iki
anahtar sozcugu de icerdigi icin stylesheet metninde substring aramasi
sabotajdan sonra da yesil kaldi. Assertion `body` **bildirimine** cevrildi ve
sabotaj tekrarlandi; ikinci turda yakalandi. Kural 2'nin tarif ettigi durum:
test adi degil, assertion'in gercekte gozledigi sey kanittir.

### 6.9/6.10. Milisaniye hassasiyeti

Durum: `UYGULANDI` (2026-08-10).

**6.9 — olculen ms degeri 3 ondalik.** Onaylı set latency istatistiklerini oyle
basiyor (t14 `44.836`, t15 `44.796`, t17 `44.801`/`44.805`, t24 `+0.011`).
Uretim global bir `%.2f` uyguladigi icin `44.801` ile `44.805` ikisi de `44.8`
oluyordu; T17'nin var olma sebebi olan format karsilastirmasi kayboluyordu.
Kural birime baglidir (`ms`, `ms/buffer`, `milliseconds`, ...), buyukluge degil:
throughput 1dp, mebibytes 2dp, spin sayisi 1dp, percent 2dp kalir.

**6.10 — tam sayi ms degeri ondalik almaz.** Ilk uygulama kosulsuz `%.3f`
uyguluyordu ve `Aggregate` tablosundaki tam sayilari `1,132.000` /`1,140.000`
yapiyordu. Onaylı t06 preview'i `1132` ve `1140` gosterir. Hassasiyet
kaybetmek bir gosterim hatasi, **uydurmak** bir dogruluk hatasidir; bu yuzden
iki yon de testte kilitli.

**Iki olcum hatam duzeltildi (kayit icin):**

1. Ilk raporumda "45 hucre bozulacak, 39'u `Test Configuration`" demistim.
   **Yanlisti.** `config_items()` degerleri `detail_value()`'dan **metin**
   olarak alir ve formatter'a hic ugramaz; o timeout parametreleri hicbir zaman
   risk altinda degildi. Dogru kapsam `value_of()` yolundan gecen
   `Aggregate`/`Measurement Result` hucreleridir. Hata kalibi: "birim ms" dar
   kanitindan "45 hucre bozulur" genis iddiasini kurmak (Kural 1).
2. Ilk sabotaj turu **gecersizdi**: tamsayi guard'i kaldirildiginda test yesil
   kaldi (`build_exit=0, test_exit=0`), cunku fixture'da `value_of` yolundan
   gecen tam sayili bir ms metrigi yoktu — guard vacuous'tu. Fixture'a t06'nin
   gercek metrik adlari (`streamon_ms_mean`, `streamon_ms_max`) eklendi.
   Ikinci sabotaj turu `1,132.000, 1,140.000` yakaladi.

Ayrica kaynak kodda `design-spec §5.14.3` diye **var olmayan** bir bolume atif
yapiliyordu (spec S1-S10 numaralandirmasi kullanir ve sayi bicimlendirmesi
hakkinda hukum icermez). Atif, gercek dayanak olan onaylı preview setiyle
degistirildi.

Sabotaj kaniti (`build_exit` ayrica olculdu):

| sabotaj | yakalayan |
| --- | --- |
| tamsayi guard'i kaldirildi | 6.10 — `1,132.000, 1,140.000` |
| `is_ms` daima false yapildi (derlenebilir form) | 6.9 — `44.81 (2dp), 44.8 (1dp) x4` |

**Acik kalan:** yedi kesirli hucre hala onaylı degerden farkli — t06
`44.81`→`44.810`, t07 `44.00`→`44.000` ve `0.40`→`0.400`, t11 `5.11`→`5.110`,
t13 `3.5`→`3.500`, `45.0`→`45.000`, `48.5`→`48.500`. Ayni birim icin 1, 2 ve 3
ondalik isteyen bu kume tek kuralla uretilemez; metrik basina tablo gerektirir
ve kullanici karari bekler.

### 6.11. T01 format sayimi tekillestirildi

Durum: `UYGULANDI` (2026-08-10). Kullanici karari: "tekillestir".

**Bulgu kullanicidan geldi:** "t01'de NV16 gozukuyor" gozlemi baska bir yere
cikti. Ayni rapor "bu kamerada kac format var" sorusuna **iki farkli cevap**
veriyordu:

| test | format sayisi | nasil |
| --- | --- | --- |
| T01 | **3** (UYVY, NV16, UYVY) | `enumerate_formats()` ham enumerasyonu yaziyordu |
| T17 | **2** | kendi listesini kurarken tekrarlari atliyordu (satir ~774) |

Kok neden: tegra isx021 surucusu UYVY'yi `VIDIOC_ENUM_FMT` index 0 **ve**
index 2'de bildiriyor, ikisi de single-plane. Yani iki buffer type cagrisi
degil, tek enumerasyon yurumesi icindeki tekrar.

**Uygulama.** Kural `append_distinct_format()` icine alindi ve `(fourcc, buffer
type)` cifti uzerinden tekillestiriyor. Buffer type anahtara **dahil**: ayni
fourcc'nin single-plane ve multi-plane altinda listelenmesi iki gercek capture
konfigurasyonudur ve `query_device()` ikisini ayni vektore yaziyor. Yalniz
fourcc'ye bakmak gercek bir modu dusururdu — ilk hatanin tersi.

Kural ayri bir fonksiyona cikarildi cunku `enumerate_formats()` gercek bir
`ioctl` gerektiriyor; **karar** gerektirmiyor. Boylece donanimsiz test edilebilir
(`tests/format_enumeration_test.cpp`).

**Fixture ham cikti olarak kaliyor.** `tests/data/device-run-2026-08-09.json`
surucunun bildirdigi duplikeyi **korur** — o, donanimin ne rapor ettiginin
kaydidir. `run_metric_coverage_test` iki sey birden dogrular: duplikenin
fixture'da hala var oldugu (yoksa kontrol vacuous olur) ve 3 girdinin 2 tekile
karsilik geldigi.

Sabotaj kaniti (`build_exit=0`, uc yon):

| sabotaj | yakalayan |
| --- | --- |
| duplike kontrolu devre disi | tekrar eklendi, 3 girdi |
| anahtar yalniz `fourcc` | multi-plane UYVY dusuruldu |
| anahtara `description` eklendi | duplike geri girdi |

Bir olcum hatasi daha duzeltildi: ilk assertion `"id": "t01..."` isaretinden
**ileriye** dilimliyordu ve 0 format satiri buluyordu — serializer `details`'i
`id`'den **once** yaziyor. Vacuous gecmek yerine FAIL verdigi icin yakalandi.

### 6.12. Girinti iki kez uygulaniyordu (olculdu)

Durum: `UYGULANDI` (2026-08-10). Kullanici bulgusu: "tablonun sol tarafindaki
yazi cok fazla girdi olarak sag tarafa alinmis"; T13/T14/T15/T17/T19-T24 icin
"hic girdi uygulanmamis"; T16 icin "tam olmasi gerektigi gibi".

Chrome 146 headless ile `--hide-scrollbars`, hem onaylı preview'ler hem de
`write_reports()` ciktisi uzerinde `getBoundingClientRect().left` olculdu.

| element | preview | uretim (once) | fark |
| --- | --- | --- | --- |
| `item-label` | 171 | 189 | +18 |
| `chart-frame` | 171 (`pl=0`) | 189 (`pl=48`) | +18 |
| `stacked-chart` | 171 (`pl=48`) | 237 | +66 |
| `cycle-row` | 219 | 285 | +66 |

**Iki ayri kok neden.**

1. `.test-body { padding: 16px }` — onaylı 26 preview'in **hicbirinde** boyle bir
   sarmalayici yok; orada `.section` kartin dogrudan cocugu ve
   `.test-card .section` zaten 16px tasiyor. Her kartta her element 16px sağa
   kaydi. `item-label`'in iki tarafta da `padding-left:32px` olmasina ragmen
   171 → 189 farki bunu gosterir: kayma konteyner seviyesinde.
2. `.has-items .chart-frame` **ve** `.has-items .stacked-chart` ikisi birden 48px
   ekliyordu. Onaylı kural: 48px belirli bir seviyeye **bir kez** uygulanir.
   Preview'lerde `chart-frame{padding:0 16px}` sade kalir ve 48px ic grafige
   aittir; **tek istisna t16**, orada `chart-frame{padding:0 16px 0 48px}` ve ici
   tekrar etmez. Kullanicinin "T16 dogru" demesinin sebebi tam olarak bu.

Duzeltme: `.test-body` padding'i sifirlandi (sinif markup'ta kaldigi icin kural
iki-yonlu sozlesme geregi duruyor), ve SVG grafikleri icin `chart-frame`
girintisi korunurken CSS bar grafiklerini sarmaladigi durumda
`:has(.stacked-chart, .thr-chart, .rel-chart)` ile iptal edildi. SVG tarafi
degismedi: T13 `chart-frame` halen `pl=48px`.

Uretim ciktisi uzerinde olculen sonuc — preview ile eleman eleman ortusuyor
(sabit +2px kartin 1px kenarligi ve durum seridi):

| element | preview | uretim (sonra) |
| --- | --- | --- |
| `chart-frame` | 171 (`pl=0`) | 173 (`pl=0`) |
| `stacked-chart` | 171 (`pl=48`) | 173 (`pl=48`) |
| `cycle-row` | 219 | 221 |

Sabotaj (`build_exit=0` ayri olculdu): iptal kurali silindi → 6.12 yakaladi;
`.test-body` padding'i geri kondu → 6.13 yakaladi.

### 6.14. T01 ve T02 icin ozel kurallar

Durum: `UYGULANDI` (2026-08-10). Kullanici karari.

**T01.** Dort degisiklik:

- Section basliklari `Device Evidence ·` onekini birakti:
  `Device Information` / `Device Capability` / `Device Pixel Formats`.
  Bu, onaylı preview'i **degistiren** bir karardir (preview `Device Evidence ·`
  diyor) — Kural 6 geregi kullanicinin en son acik karari ustte.
- `Driver`/`Card`/`Bus` degerleri normal yazi fontuna dondu. Uretim
  `.kv-row dd` icin `JetBrains Mono` kullaniyordu; onaylı preview'de
  `font-family` override'i yok ve `.kv strong{font-weight:400}`.
- `SUPPORTED` vurgusuz: `.capability-row dd` 800 → 400. Renk verdigi tasiyor,
  kalinlik tasimiyor. Yesil korundu.
- `Backend support (VIDIOC_REQBUFS accepted)` → `Backend Support` (buyuk S) ve
  kabul edilen ioctl kendi satirinda (`Accepted ioctl`). Kullanicinin genel
  geri bildirimi: rapor `Backend supported` yaziyordu, `Backend Support` olmali.

**T02.** Controls/Writable/Read-only seridi kaldirildi. Onaylı preview
`Control Evidence` + tablodan olusur, serit yok (Kural 4b). Serit ayni zamanda
**yanlisti**: `Read-only`, `{"writable_count", "read_only"}` sirasini cozuyordu,
yani writable sayisini yaziyordu — 2026-08-10 kosumunda 14 writable + 1 read-only
tasiyan bir tablonun ustunde "13" ve "13". Iki sayi da `Measurement Result`'ta
kaliyor (Kural 4c).

### 6.15. PASS kartlarinda giris metni kaldirildi

Durum: `UYGULANDI` (2026-08-10). Kullanici karari: "pass olan bir test
sonucunda asla yazi bu sekilde girise yazilmaz, aslinda bu tarz yazi hic
yazilmaz. Bunu not almistik. Hata yapmissin."

Olculdu: onaylı `t07` ve `t11` hardware-trigger kartlarinda `<p>` **yoktur**
(tek `<p>` preview etiketinin kendisi, karta ait degil).

- T07: `Requested and allocated buffer counts matched...` + `100/100 frames
  captured` kaldirildi. Bilgi kaybolmuyor (Kural 4c): tahsis araligi
  `Latency by buffer count` tablosunda satir satir, toplam ise
  `Measurement Result` icindeki `Capture success` oraninda.
- T11: `The 4 KiB and 64 KiB figures are repeated reads...` kaldirildi. Ayrimi
  artik legend tasiyor (`Full frame (primary)` / `Cache-sized reads`) ve her bar
  kendi seri renginde.

`requests.empty()` durumundaki `Unavailable` korundu: hic satiri olmayan bir
kart bunu soylemek zorunda.

### 6.16. T11 kopya bolgesi etiketleri okunabilir hale geldi

Durum: `UYGULANDI` (2026-08-10).

Kosum `mmap_full` / `mmap_4k` / `mmap_64k` yaziyordu — bunlar **metrik
anahtarlari** (`<label>_mbps`). Onaylı preview `Full frame` / `4 KiB sample` /
`64 KiB sample` gosteriyor. Teknik id degismedi (proje kurali); gosterim adi
`t11_display_label()` icinde boyut sonekinden uretiliyor.

### 6.17. Milisaniye hassasiyeti metrik basina belirlendi

Durum: `UYGULANDI` (2026-08-10). Bu, onceki turda "kullanici karari bekliyor"
diye birakilan **yedi kesirli hucre** maddesidir.

Onaylı set olculdugunde tek bir kuralin bunu **uretemeyecegi** ortaya cikti —
ondalik sayisi birimin degil, **metrigin ne olctugunun** ozelligi:

| ondalik | nerede | ornek |
| --- | --- | --- |
| 3 | t14/t15 dagilim istatistikleri | `44.778`, `44.800`, `0.012`, `55.164` |
| 2 | t06/t07/t11 tek bildirilen figur | `44.81`, `44.00`, `0.40`, `5.11` |
| 1 | t13 poll-timeout sinirlari | `48.5`, `3.5` |
| 0 | konfigurasyon girdileri | `Warmup frames 10`, `safe margin 5` |

3 ondalik gerekcesi olculdu: t15 `44.796` ile `44.799` sutunlarini
**karsilastiriyor**; iki ondalikta ikisi de `44.80` olur ve testin varlik sebebi
kaybolur.

Uygulama `ms_decimals_for()` icinde metrik adi desenlerine bagli. Desenler
satir basina bir kural olarak `MsPrecisionRule` dizisinde: cıplak string'lerden
olusan bir suslu parantez listesi, `metric_name_contract_test`'in statik
taramasina `{label, metric, ...}` verdict spec'i gibi gorunuyor ve runner'dan
`estimated_copy` diye bir metrik kaydetmesini istiyordu.

Tam sayi kurali korundu: deger tam ise tam yazilir, yani uydurma `.000`
uretilmiyor. **Bu, onaylı t13 ile celisir** — orada `float` tipli satirlar
`45.0` ve `44.0` gosteriyor. Celiski asagida P3 geregi raporlanmistir, kod
uydurma-ondalik yasagini koruyor.

### 6.7. T11 grafigi onaylı konvansiyona cevrildi

Durum: `UYGULANDI` (2026-08-09). Kullanici karari: "gorsel sonuc ayni degil;
nasil dizayn edilmek istendiyse o sekilde olmali. Genel convention
`refactored_previews`'deki gibi olmali."

Onceki turda bunu "gorsel sonuc bozuk degil" diyerek kapsam disi
birakmistim — **yanlisti**. Onaylı `t11-preview.html` grafigi paylasilan
yatay-bar sozdagarciyla cizer; uretim ise T08'in doygunluk ailesini
(`load-row`/`load-track`/`load-bar`/`load-value`) odunc almisti.

| | onaylı | uretimdeki (once) |
| --- | --- | --- |
| satir | `bar-row` > `bar-label` + `bar-track` > `bar-fill.bar-full\|bar-cache` + `bar-info` | `load-row` > `strong` + `load-track` > `load-bar.t11-copy-bar` + `load-value` |
| legend | `legend-full` / `legend-cache` swatch'li `chart-legend` | **yok** |
| eksen adi | `scale-name` — "Copy throughput (mebibytes per second)" | **yok** |
| deger kolonu | `bar-info`, 74px, tabular | `load-value`, 112px |

Olculen sonuc: `bar-full` `#2e6fa3`, `bar-cache` `#71879a` (ikisi de
preview'den birebir), degerler `1,027.13` / `1,033.87` / `1,025.31`.

**Olu kural temizligi (iki yonlu kontrolun ters bacagi):** `load-row`,
`load-track`, `load-bar`, `load-value`, `t11-copy-bar`, `t11-full-bar`,
`t11-cache-bar` kaldirildi. Olculdu: T08 kendi doygunluk halini `queue-row` ve
`slots` ile ciziyor, yani `load-*` ailesini **hicbir sey** kullanmiyordu —
gercek cihaz raporundaki 4 gecisin tamami T11'e aitti.

**Bayat assertion yakalandi (Kural 7):** `test_content_registry_test`
`count_of(html, "t11-copy-bar") == 3` sayiyordu. O sinif artik yok; assertion
paylasilan `bar-fill bar-full` + `bar-fill bar-cache` sayimina cevrildi ve
ikinci bir kontrol iki cache satirinin ikincil seri oldugunu dogruluyor. Eski
haliyle birakilsaydi hicbir sey cizmeyen bir grafikte sessizce yesil kalirdi.

Sabotaj: grafik `load-*` ailesine geri dondurulunca `report_css_contract` yedi
sinifi "kuralsiz" olarak, `test_content_registry` de bar sayimini yakaladi
(`build_exit=0`).

### 6.3. SKIP govdesi

Onayli preview'lerdeki **yedi** SKIP kartinin tamami yalnizca banner tasiyor
(`h4=0`, `section=0`, 86-104 karakter). Canli T25 SKIP ise `h4=4`, `section=3`,
1383 karakter ile tam govde uretiyor. Kural 4b geregi bu fazlalik kaldirilir.

## 6. Kalan is

Faz 0-5'in tamami uygulandi ve kontrol listesinde acik madde kalmadi.

**Tek acik is: cihaz kosumu dogrulamasi.** Kural 10'un ucuncu seviyesi —
kullanici gercek donanimda kosup raporu paylasacak. O zamana kadar Faz 3b
`UYGULANDI` kalir, `DOGRULANDI` olmaz.

### 6.19. T05 verdict kurali duzeltildi

Durum: `UYGULANDI` (2026-08-10). Kullanici karari: secenek A.

**Kural.** `recovery_ok == 0` → `FAIL`; `0 < recovery_ok < min_recovery_ok` →
`WARN`; esigi karsilarsa `PASS`. DQBUF STREAMOFF'tan sonra basarili olursa veya
re-STREAMON basarisiz olursa `FAIL` — bunlar kurtarma sonucundan **once**
kontrol edilir.

**Celiskinin iki tarafi olculdu.** Onaylı `t05-preview.html` uc senaryoda da
`FAIL` gosteriyor ve iki ayri cumle kullaniyor: free-run "The driver returned a
frame after STREAMOFF", hardware/software-trigger "DQBUF was correctly rejected
after STREAMOFF, but the restarted stream delivered no recovery frames (0/3)".
Preview'de **hic** `result-warn` veya `test-card warn` yok. Buna karsilik
`docs/backend/tests/t05-pollerr-handling.md` verdict tablosu WARN'i acikca
tanimliyordu.

Cozumun dayanagi: `recovery_ok = 0` **partial degil**. Doc'un kendi Failure
Modes tablosu bunu ayri satirda "the pipeline is stalled" olarak listeliyor.
Ayrica T05 tek bir `min_recovery_ok` esigi tasiyor; gercekten WARN kademesi olan
testler (T03, T24) `pass_*`/`warn_*` cifti tasiyor, yani WARN bandi icin
altyapi yok. Secenek A doc'un WARN bandini tarif ettigi duruma (kismi kurtarma)
birakip sifir durumunu preview'e uyduruyor.

**Uygulama.** Karar `pollerr_recovery_verdict()` icine alindi
(`diagnostic_runner.hpp`), cunku test govdesi gercek cihaz gerektiriyor;
`multi_buffer_verdict`/`pulse_width_verdict` ile ayni kalip. Ozet cumleler de
preview'in ifadesine cevrildi ve `/3` sabiti `RECOVERY_CAP`'e baglandi — eski
metin yapilandirilabilir sayima ragmen her zaman "/3" yaziyordu.

Doc guncellendi: verdict tablosu artik kodla ayni ve degisikligin gerekcesini
tasiyor.

**Sabotaj — biri once bosa cikti, bu bir bulgudur.** Ilk turda govdenin kendi
`else → Warn` merdivenine geri donmesi **butun testleri yesil biraktı**:
`verdict_rules_test` yalnizca saf fonksiyonu olcuyordu, cagri yerini kimse
gozlemiyordu. Kural doğruydu ve **kullanilmiyordu** — Kural 9'un tarif ettigi
vacuous guard. Kapatmak icin runner kaynagini tarayan bir delegasyon kontrolu
eklendi (`metric_name_contract_test` ayni kalibi kullaniyor); `WORKING_DIRECTORY`
CMake'de repo kokune ayarlandi. Tarama yalnizca **verdict bolgesini** olcuyor:
oturum kurulumu basarisiz oldugunda erken `return` mesru sekilde `Fail` yaziyor
ve bu surucu davranisi hakkinda bir verdict degil.

| sabotaj | build_exit | sonuc |
| --- | --- | --- |
| `recovery_ok <= 0` dali `&& false` ile kapatildi | 0 | iki assertion yakaladi |
| govde inline WARN merdivenine dondu (1. tur) | 0 | **bosa cikti** — guard eklendi |
| govde inline WARN merdivenine dondu (2. tur) | 0 | delegasyon kontrolu yakaladi |

Production dogrulamasi: cihaz kosumunun kendi kaniti (`dqbuf_failed=1`,
`restreamon_ok=1`, `recovery_ok=0`) `write_reports()` uzerinden gecirildi —
`card class=fail`, `status=FAIL`, banner `[fail]` ve cumle onaylı preview ile
birebir ayni.

### 6.20. T08 karti onaylı tasarima gore yeniden yazildi

Durum: `UYGULANDI` (2026-08-10). Kullanici karari: secenek A (tam yeniden yazim).

Kullanici grafik isimlendirmesini isaret etti; olcum daha genis bir sapma gosterdi
ve tam kart yeniden yazimi secildi.

| konu | once | onaylı / simdi |
| --- | --- | --- |
| grafik | `metric-chart-title` "Queue After Saturation 2 count allocated buffers" + legend | **grafik yok** |
| slot markup | iki kolonlu ortali ERROR/READY kutulari | `queue-row` > `queue-label` + `slot-strip` > `slot` |
| slot icerigi | uydurma bir ERROR + bir READY | buffer basina index / state / sequence / cozulmus flag |
| sira | slot'lar once, tablo sonra | tablo once, slot'lar sonra, Aggregate en son |
| `Error flag mask` | `error_flag_total` → `2` | `0x2041`, detayi `ERROR \| MAPPED \| MONOTONIC` |
| `Buffer retention` | `frames_available_A` → `2` | iki variant toplami → `4/4` |
| config satirlari | 6 satir, "Variant A: 100 triggers at 100ms" | onaylı 8 satir, tetik sayisi ve aralik ayri |

**Runner tarafinda gercek bir eksik vardi.** Kosum yalnizca **hatali** buffer'lar
icin kanit yaziyordu (`Error flag buffers: ...`). Onaylı kart her buffer'i
gosteriyor — READY olanlari da — cunku "2'den 1'i flag tasidi" ancak ikisi birden
cizilince okunur. Yeni `slot: <variant>|<index>|<sequence>|<flags>` satiri her
tutulan buffer icin yaziliyor. Ayrica `errors=` alani artik 0 durumunda da
yaziliyor (eksik alan renderer'da "bilinmiyor" gibi okunuyordu) ve `buffers=2`
sabiti `BUF_COUNT`'a baglandi.

**Yan bulgu — `split_unit()` hex degeri bozuyordu.** `0x2041` icin rakam taramasi
`x`'te duruyor, deger `0` ve birim `x2041` oluyordu: bir buffer flag'i **sifir**
olarak raporlaniyordu. Hex literal tek token olarak ele alindi; bu T02'nin kontrol
id'leri gibi diger hex alanlari da koruyor.

**Sabotaj — biri yine bosa cikti.** T05'teki ayni kalip: runner'in slot yazimini
`if (buf.flags & V4L2_BUF_FLAG_ERROR)` icine almak **butun testleri yesil
biraktı**, cunku her fixture kendi `slot:` satirlarini elle yaziyor — renderer
kapsanmis, runner'in emisyonu kapsanmamis. Kaynak taramasi ile guard eklendi.

| sabotaj | build_exit | sonuc |
| --- | --- | --- |
| `MONOTONIC` cozumu `&& false` ile kapatildi | 0 | iki test yakaladi |
| slot yazimi error flag'ine baglandi (1. tur) | 0 | **bosa cikti** — guard eklendi |
| slot yazimi error flag'ine baglandi (2. tur) | 0 | delegasyon/emisyon guard'i yakaladi |

Production dogrulamasi (taze binary ile, cihaz kosumunun kendi verisi): grafik
yok, iki `queue-row`, iki `slot-strip`, iki READY + iki ERROR slot, mask `0x2041`,
retention `4/4`. Geometri onaylı preview ile ortusuyor (`item-label` 173/171,
`queue-row` `pl=48px` her ikisinde).

Emekli edilen siniflar: `slots`, `queue-value`, `t08-legend-error`,
`t08-legend-ready`, `evidence-row` (+ `.flags`/`.raw`). Iki-yonlu CSS sozlesmesi
kullanilmayan kural raporunda artik hicbirini listelemiyor. `.slots`'a atifta
bulunan eskimis bir yorum da duzeltildi.

### 6.18. Raporlanan, duzeltilmeyen celiskiler ve kapsam disi bulgular

P3/P4 geregi: olculdu, raporlandi, **uygulanmadi**.

> **Cozuldu 2026-08-10 (secenek A).** Asagidaki T05 celiskisi kullanici karariyla
> kapatildi; kayit `6.19`'da. Bolum, kararin dayandigi olcumu korumak icin
> duruyor.

**T05 verdict celiskisi.** Ayni kanit onaylı preview'de `FAIL`, canli kosumda
`WARN`. Karar noktasi olculdu — `diagnostic_runner.cpp:2115`:

```text
dqbuf_failed=1, recovery_ok=0, min_recovery_frames=2
  dq_ret < 0 && re_ok && recovery_ok >= min_rec  -> false  (0 >= 2 degil)
  dq_ret >= 0                                    -> false  (DQBUF dogru sekilde basarisiz)
  else                                           -> WARN
```

Bu bir esik uyusmazligi **degil**: `FAIL` dali yalnizca `dq_ret >= 0` icin
ayrilmis, yani DQBUF dogru davranip kurtarma sifir oldugunda kodun `FAIL`'e
giden **hicbir yolu yok**. Duzeltme verdict mantigini degistirmek demektir
(P2), o yuzden karar kullanicidadir.

**Milisaniye tam sayi celiskisi — kapandi 2026-08-10, secenek A: kod degismez.**

Kullanici karari, olcumden sonra: onaylı sette **tutarli bir kural yok**, o yuzden
kod taklit edilecek bir ilke bulamiyor. Tam deger + `float` tipi kombinasyonu iki
farkli sekilde basiliyor:

| test | satir | tip | deger |
| --- | --- | --- | --- |
| t13 | Reliable cliff | float | **45.0** |
| t13 | First miss | float | **44.0** |
| t13 | Timeout headroom | float | **45.0** |
| t16 | Edge margin | float | **1.0** |
| t03 | First-frame mean | float | **145** |
| t06 | Open + STREAMON mean | float | **1132** |

Ayni girdi, farkli cikti. t16 ikisini **tek kartta** gosteriyor: ayni 1 ms degeri
`Edge margin` satirinda `1.0`, `Observed minimum tested` satirinda `1`. Yani
ondalik degerden de, olculmus olmasindan da turetilemiyor — satir bazinda
editoryal bir secim.

Bir ara hipotezim ("olculen vs yapilandirilan") **yanlisti** ve olcum onu curuttu:
t03'un `145`'i ve t06'nin `1132`'si de olcumdur ve ciplak basiliyor.

Reddedilen alternatifler: metrik adina gore `.0` zorlamak (dort hucreyi duzeltir
ama kurali bir istisna listesine cevirir ve uydurma-ondalik yasagiyla kavramsal
olarak celisir); preview'lerdeki dort hucreyi ciplaga cevirmek (onaylı tasarima
dokunur). Kodun tutarli olmasi, tutarsiz bir tasarimi taklit etmesine tercih
edildi. Sapma bilinerek kabul edildi: t13'te uc hucre, t16'da bir hucre.

**T08 kapsam disi (P4) — cozuldu 2026-08-10, kayit `6.20`.** Asagidaki olcum
kararin dayanagi oldugu icin duruyor. Kullanici grafik isimlendirmesini isaret etti ama
olcum daha buyuk bir sapma gosterdi: onaylı `t08` farkli tablo siniflari
(`variant-header`/`variant-row`), buffer basina zengin slot markup'i
(`slot-index`/`slot-state`/`slot-seq`/`slot-flag`), grafik basligi **ve** legend
yokluğu, ve tablo-once-slot-sonra sirasi kullaniyor. Canli kart slot'lari once
veriyor. Bu tam bir kart yeniden yazimi; istenen maddenin sinirlarini asiyor.

**Kuralsiz uc sinif.** `flags` (T10), `ok` (T13), `warn-text` (T20) kendi
kuralina sahip degil ama olculdugunde ebeveyn kuralindan 11px/9px aliyor —
gorsel bozukluk yok, hicbiri preview'de tanimli degil.

**HTML dosya adi tarihi.** `output/` icinde JSON ve MD `2026-08-10_00-37-36`,
HTML ise `2026-08-09_10-31-39` adini tasiyor; icerigi yeni kosum. Bir kosumun
uc artifact'i ayni basename'i tasimali.

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
