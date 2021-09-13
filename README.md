UVOD – Ideja projekta i tekst zadatka okvirno
Ovaj projekat ima zadatak da simulira sistem koji kontroliše prozore u automobilu( manuelno i automatski). Kada se vrši automatska kontrola, podrazumjeva se da se nakon određene brzine automobila prozor ne smije držati otvoren i šalje se signal da se prozori zatvore. Kada se brzina spusti ispod propisane, prozori se vraćaju na prethodno stanje. Manuelna kontrola se rješava pomoću dugmića iz LED_Bars programa. Kao okruženje koristi se VisualStudio2019.  Zadatak ovog projekta, osim same funkcionalnosti je bila i implementacija Misra standarda prilikom pisanja koda. 
Pratimo podatke sa senzora za sva 4 prozora, kao i brzinu automobila. Posmatramo vrijednosti koje se dobijaju iz UniCom softvera  sa kanala 0. Usrednjimo zadnjih 10 odbiraka vrijednosti sa senzora brzine, kako bi se dobio realniji podatak o prosječnoj brzini.
Realizujemo komunikaciju sa simuliranim sistemom. Naredbe i poruke koje se salju preko serijske veze treba da sadrze samo ascii slova i brojeve, i trebaju se zavrsavati sa carrige return (CR),  tj. brojem 13 (decimalno), čime se detektuje kraj poruke.  Naredbe su:
a.1) Na komande rada prozora MANUELNO i AUTOMATSKI, kojima se mijenja režim rada prozora, sistem treba da odgovori sa OK.

a.2) Pomocu komande NIVO <broj> zadajemo nivo prozora 0-skroz spušten, 1-skroz podignut.

a.3) Pomocu komande BRZINA <broj> potrebno je zadati maksimalnu brzinu pri kojoj prozori smiju biti otvoreni.

b) Slanje ka PC-iju (UniCom  kanal 1) trenutne vrijednosti stanja prozora i brzine, režim rada prozora. Kontinualno šaljemo podatke periodom od 5000ms.

3. Ako je trenutni režim rada MANUELNO, podrazumijeva se da se nivo prozora reguliše preko prekidača. U tu svrhu  podešavamo jedan stubac na LED baru kao ulazni, a drugi kao izlazni. Ulazni stubac simulira prekidače, tj. zavisno koja je LED uključena  tada se podiže prozor. Ako je aktivan signal za podizanje prozora, uključujemo jednu izlaznu LED diodu na LED baru.

4. Ako je trenutni režim rada sistema AUTOMATSKI, podrazumijeva da se prozori kontrolišu prema senzoru brzine. Pratimo srednju vrijednost brzine. Zavisno od vrijednosti brzine postavljamo prozore u traženo stanje.

5. Na LCD displeju prikazaujemo trenutnu vrijednost brzine i režim rada (AUTOMATSKI rezim sa brojem 0, a MANUELNI sa 1), osvježavanje podataka na svakih 1000ms.Pored toga pamtimo maksimalnu izmjerenu vrednost brzine od uključivanja sistema i pritiskom na odgovarajući “taster” na LED baru , prikazujemo maksimalnu vrijednost pored trenutne vrijednosti brzine.

   
Periferije
Periferije koje je potrebno koristiti su LED_bar, 7seg displej i AdvUniCom softver za simulaciju serijske komunikacije. Prilikom pokretanja LED_bars_plus.exe, navesti rR kao argument da bi se dobio led bar sa 1 ulaznim i 1 izlaznim stupcem crvene boje. Prilikom pokretanja Seg7_Mux.exe navesti kao argument broj 9, kako bi se dobio 7-seg displej sa 9 cifara. Što se tiče serijske komunikacije, potrebno je otvoriti i kanal 0 i kanal 1. Kanal 0 se automatski otvara pokretanjem AdvUniCom.exe, a kanal 1 otvoriti dodavanjem broja jedan kao argument AdvUniCom.exe 1.
Opis taskova
SerialSend0_Task
Ovo se odnosi na kanal 1 tj. na sve informacije koje se prikazuju u velikom box-u.
SerialReceive0_Task
Ovaj task ima za zadatak da obradi podatke koji stižu sa kanala 0 serijske komunikacije. Podatak koji stiže je u vidu poruke  npr. 1 0 1 1 120+. To je podatak o trenutnom stanju prozora i trenutnoj brzini. Karakteri se smeštaju u niz koji se smješta u red, kako bi ostali taskovi taj podatak imali na raspolaganju. Ovaj task "čeka" semafor koji će da pošalje interrupt svaki put kada neki karakter stigne na kanal 0.
SerialReceive1_Task
Ovaj task ima za zadatak da obradi podatke koji stižu sa kanala 1 serijske komunikacije. Naredbe koje stižu su formata manuelno+, automatski+, brzina 150+, nivo 3 0+. Task takođe kao i prethodni čeka odgovarajući semafor da bi se odblokirao i izvršio. Taj semafor daje interrupt svaki put kada pristigne karakter na kanal 1.
LED_bar_Task
Task koji na osnovu pritisnutog tastera određuje položaj prozora i ispisuje na 7seg displeju informaciju o maksimalnoj brzini. Ovaj podatak tj. string smještamo u red koji dalje ide u obradu senzora. 
Obrada_senzora
U ovom tasku se prvo detektuje o kom prijemu se radi. Nakon toga obradjuje podatke koji se  dobijaju sa SerialReceive0_Taska, SerialReceive1_Taska ili LED_bar_Taska.
Display_Task
Ovaj task nam služi za ispis na seg7 displeju. Displej osvježavamo svakih 1s. Na nultoj poziciji ispisujemo režim rada, pa razmak i ide ispis trenutne brzine, pa razmak i ispis maksimalne brzine ako je pritisnut taster na LED baru predviđen za to.
TimerCallBack
Funkcija za brojač se aktivira svakih 200ms. Ima tri funkcionalnosti:
Svakih 200ms šalje “T” na kanalu 0
Svakih 5s šalje podatke na kanal 1
Svakih 1s osvježava displej