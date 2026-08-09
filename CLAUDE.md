# v4l2-camera-diagnostic — calisma talimatlari

## Evidence And Completion Protocol

Bu bolum kullanicinin acik talimatidir. Bu projede ayni hata kalibi tekrar
ettigi icin yazildi: dar bir kanit uzerine geniş bir tamamlanma iddiasi kurmak.
Her oturumda gecerlidir.

### 1. Genis iddia, dar kanitla kurulamaz

Header yoklugu "istek reddedildi" **degildir**. Dosya sayisi "preview uygulandi"
**degildir**. Grafik sayisi "grafik dogru" **degildir**.

Gercek ornek: `/api/dmesg?download=1&run=unknown` icin yalnizca
`Content-Disposition` yoklugu test edilmisti. Sunucu `journalctl`'i yine
calistirip kernel log'unu 200 ile donduruyordu. Test yesildi, guvenlik acigi
duruyordu.

### 2. Test adi kanit degildir

Her test icin assertion'in **gercekte gozledigi** sey acikca belirtilir: status,
body, DOM, dosya, callback, command count veya piksel ciktisi.

### 3. "Tamamlandi" kelimesi kontrolludur

Bir maddede acik fark, eksik renderer, basarisiz test veya **bekleyen kullanici
gorsel onayi** varsa tamamlandi denmez. Dogru ara statu kullanilir.

### 4. "Preview degismedi" implementation tamamlandi demek degildir

Onaylı preview dosyalarinin degismemesi, canli renderer'in o icerigi urettigini
**kanitlamaz**. Uygulama, production `write_reports()` ciktisi uzerinde olculur.

Kanit manifesti **iki yonlu** dogrulanir: README'nin adlandirdigi her dosya var
mi, ve dizindeki her dosya adlandirilmis mi. Tek yonlu kontrol, tabloda yazili
olup dosyasi olmayan bir artifact'i gecirir.

### 4b. Fazlalik da bir sapmadir

Onaylı preview'de olmayan bir grafik, tablo veya bolum "fazlalik" diye kabul
edilemez: rapor musteriye gidiyor ve orada onaylanmamis icerik bulunuyor.
Eksik icerik gibi bu da kaldirilir veya onaya sunulur.

Gercek ornek: chart selector `_mean_ms`/`_p95_ms`/`_max_ms` ailesini otomatik
sectigi icin sekiz test preview'inde bulunmayan bir nokta grafigi uretiyordu.

### 4c. Icerik kaldirirken yan etkiyi ara

Bir gosterim yolu kaldirilinca icine gomulmus **bulgular** da gider. Onaylanmamis
grafikler kaldirildiginda "Not measured" bildirimi de kayboldu: `render_test_metrics`
icindeydi, yani grafiksiz testlerde artik hic uretilmiyordu.

### 5. Onaylı icerik kapsam disina cikarilamaz

Preview'deki teste ozgu tablo, grafik ve aciklamalar acikca onaylandiysa generic
fallback ile degistirilemez. "Teste ozgu kalir" ifadesi bunlarin kapsam disi
birakilmasi anlamina **gelmez**; ortak dis kabugun icinde teste ozgu renderer
bulunmasi anlamina gelir.

### 6. Karar otoritesi sirasi zorunludur

1. Kullanicinin en son acik karari
2. `docs/report-ui-design-spec.md`
3. `docs/implementation-plan.md`
4. Preview artifact'leri
5. Mevcut kaynak kod

Alt siradaki kaynak ust siradaki karari **daraltamaz**. Kaynak kodun bir seyi
yapmiyor olmasi, o seyin istenmedigi anlamina gelmez.

### 7. Eski davranis taramasi zorunludur

Yeni kod eklendikten sonra eski CSS, label, endpoint, fallback, serializer,
assertion ve dokuman hukumleri aranir.

Gercek ornek: kartlar `SKIP` gosterirken Overview tablosu `to_string(status)`
ile `skipped` yaziyordu ve `text-transform: uppercase` kullaniciya `SKIPPED`
gosteriyordu. Buyuk harfli kaynak substring arayan test bunu goremedi.

### 8. Negatif yol butun yan etkileriyle olculur

HTTP status, error body, hassas cikti, command invocation, dosya degisimi,
artifact/history/cache etkisi **ayri ayri** dogrulanir.

### 9. Sabotaj yalniz yeni binary ile gecerlidir

Build exit code kontrol edilmeden ve sabotajin hedef assertion'a ulastigi
gosterilmeden sonuc kanit sayilmaz. Stale binary sonucu **yasaktir**.

Derlenmeyen sabotaj 0-FAIL verir ve bu vacuous bir sonuctur. `-Werror` altinda
kullanilmayan fonksiyon/parametre hatasi cok kolay olusur; `&& false` / `|| true`
gibi derlenebilir formlar kullanilir. `tail -1` gibi bir pipeline `$?`'yi
bozar — build exit kodu ayrica olculur.

### 10. Gorsel is iki seviyede kapanir, ucuncusu kullanicidadir

1. DOM sozlesmesi
2. Production HTML — `write_reports()` ciktisi uzerinde olcum

**Ajan PNG, screenshot veya PDF uretmez** (kullanici karari, 2026-08-08).
Gorsel dogrulamanin ucuncu seviyesi kullanicinin **gercek cihazda** aldigi
kosum sonucudur: kullanici raporu paylasir, kontrol o zaman yapilir.

Bu, 1 ve 2'yi gevsetmez. Bir madde ancak DOM sozlesmesi testle kilitlenmis
**ve** production HTML uzerinde olculmusse `UYGULANDI` olur; cihaz kosumu
gelene kadar `DOGRULANDI` denmez.

### 11. Soru sorma esigi yuksektir

Rutin karar kullanicinin degil, benim isimdir. Yalnizca farkli okumalar maddi
olarak farkli ise yol acacaksa sorulur.

## Karar Koruma Protokolu (P1-P8)

`docs/implementation-plan.md` icindeki "Calisma ve Karar Koruma Protokolu"
bolumu gecerlidir. Ozetle:

- **P1** Her adim oncesi Decision Lock yazilir.
- **P2** Onaylı karar optimize edilmez; reddedilen alternatif uygulanmaz.
  Teknik problemde durup secenek sunulur.
- **P3** Celiskide uygulama yapilmaz; olcumle raporlanip sorulur.
- **P4** Scope Freeze: yalnizca istenen yuzeyler. Kapsam disi kusur
  **raporlanir**, duzeltilmez.
- **P5** Tamamlandi demeden once Conformance Matrix.
- **P6** Plan statusu kendiliginden yukseltilmez
  (preview → `ONAY BEKLIYOR`; kullanici onayi → `TASARIM ONAYLANDI`;
  kod+test → `UYGULANDI`/`DOGRULANDI`).
- **P7** Yetki sirasi: yukaridaki Kural 6.
- **P8** Sabotaj testleri reddedilen yerlesimlerin geri gelmedigini de
  dogrular; her sabotajda build exit code kontrol edilir.

## Sabit kisitlar

- `docs/report-ui-design-spec.md` **kullanicinin belgesidir**. Degistirmeden
  once sorulur; celiski cozulmez, yuzeye cikarilir.
- Onaylı test icerigi preview'lerde degistirilmez.
- Korlemesine toplu replace yapilmaz. Gercek PDF referanslari
  (`Export PDF`, `.pdf` dosya adlari, browser-print metni) korunur.
- markdownlint: MD012 global olarak kapatilmaz;
  `report-ui-design-spec.md` `.markdownlintignore`'a eklenmez.
- Teknik test id'leri ve dosya slug'lari degismez.
- Her davranis degistiren madde **once** testle kilitlenir (test-first) ve her
  guard gecici geri alma ile yuk tasidigi kanitlanir.

## Guvenlik siniri (DMESG ve artifact erisimi)

- Sunucu ayricaliksiz kalir; `adm`/`systemd-journal` modeli korunur.
- Shell'e **kullanici girdisi eklenmez**. `sudo journalctl` icin yeni
  privileged kod veya shell yolu **eklenmez**.
- Istemci filename gonderip `Content-Disposition` belirleyemez; ad sunucuda
  run/index metadata'sindan uretilir.
- Bilinmeyen veya historical kaydi olmayan run **reddedilir** — yalnizca header
  atlanmaz, istegin kendisi 4xx doner.
- Run id asla dosya yolunun parcasi olmaz; yalnizca `runs-index` kaydini exact
  match ile secer.
- Bir dosya ancak (a) run'in kaydi onu bildiriyorsa **ve** (b) cozulen yolu o
  run'in kendi `web-run-{id}` dizini altindaysa servis edilir. Report-root
  siniri tek basina yetersizdir (cross-run symlink onu gecer).

## CI kapilari

CI'nin **gercek** komutlari kullanilir, filtre eklenmez:

```sh
cpplint --recursive source/backend/            # CPPLINT.cfg otomatik uygulanir
find source/backend \( -name "*.cpp" -o -name "*.hpp" \) -print0 \
  | xargs -0 clang-format-18 --dry-run --Werror

# DIKKAT: yukaridaki clang-format komutu CI'nin komutudur ve yalnizca
# source/backend'e bakar. pre-commit hook DAHA GENIS: staged olan HER .cpp/.hpp.
# Yani tests/ altinda birakilan bir format ihlali CI komutundan gecer, commit'te
# patlar. Hook'un kendi komutuyla dogrulanir:
git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|hpp)$' \
  | xargs clang-format-18 --dry-run --Werror
npx markdownlint '**/*.md'                     # repo kokunden
# eslint: --max-warnings=0 with an equals sign; the space form is rejected here
cd source/frontend && npx tsc --noEmit && npx eslint src --max-warnings=0 && npx vitest run
cmake -S . -B build-strict -DV4L2DIAG_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build-strict -j8 && (cd build-strict && ctest)
```

`ctest --test-dir build-strict` bu projede test bulamaz; `build-strict` icinden
calistirilir.

Kanit dizini uretildiyse manifest de dogrulanir — kanit kendi manifestiyle
tutarli olmadan gorsel onaya sunulamaz:

```sh
python3 docs/assets/source-render/manifest_check.py
```

## Kanit dizini tazeligi

> **Durum 2026-08-08:** `docs/assets/source-render/` su an **arac dizinidir**;
> uretilmis PNG/PDF/txt yoktur. Eski artifact'ler `docs/assets/previews/`
> setine karsi olculmustu, o dizin artik yok. Yeni hedef
> `docs/assets/refactored_previews/`; artifact'ler Faz 3b'de uretilecek.
> Asagidaki kural o uretimden itibaren yeniden yururluge girer.

`docs/assets/source-render/` icindeki her PNG/PDF/txt, **committed** kod
tarafindan uretilmis olmali — ara denemeler icin `/tmp` kullanilir, bu dizin
degil. Bir renderer/fixture degisikligi yapildiktan sonra kanit dizinine
donmeden once mutlaka:

```sh
docs/assets/source-render/refresh.sh
```

calistirilir. Aksi halde dizin, artik var olmayan bir build'in kanitini tasir
ve dosya adinda bunu soyleyen bir sey olmaz — Kural 4 tam bu hatayi tarif eder.

Tazeligin kaniti: `refresh.sh`'i tekrar calistirip ciktinin (`md5sum`) veya
`deviation-inventory.txt`'nin degismedigini gostermek. Degisirse committed
dosyalar bayattir ve refresh sonucuyla degistirilir.

## Preview render

Chrome surumu **tam olarak** kaydedilir (`docs/assets/previews/RENDER.md`);
render byte duzeyinde surume baglidir. `--hide-scrollbars` **zorunludur**:
atlanirsa sayfa genisligi 1280 yerine 1265 olur ve her olcum kayar.
