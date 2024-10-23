
# Документация по структуре REDO логов

Каждый redo лог состоит из блоков фиксированного размера.
По умолчанию размер блока 512 байт. Но есть так же варианты 1024 и 4096.
Это означает, что размер redo файла будет кратен одному из этих трёх чисел. 

## Header

Первые два блока отдельного redo файла представляют заголовок.

### Блок 1 (Заголовок файла)

![](https://github.com/Igor743646/OpenLogReplicator/blob/master/documentation/ru/log-structure/images/block1.png?raw=true)

Структура:

```rust
struct Block1 {
    u8 block_flag;
    u8 type_of_file; // 0x22 for redo log
    padding[18];
    u32 block_size;
    u32 number_of_blocks;
    u8 magic_number[4];
    padding[512 - 32];
};
```

Тут `block_flag` указывает на то, что данный блок предоставляет информацию о самом файле, как таковом.

Чтобы различать различные файлы Oracle, второй байт `type_of_file` используется в качестве индикатора типа файла. 
Например, в версии 10g Release 2 файлы данных имеют тип **0xA2**, управляющие файлы имеют тип **0xC2**, и мы можем видеть
из приведенного шестнадцатеричного дампа, что файлы журнала повтора имеют тип **0x22**.

`block_size` содержит информацию о размере блока записи в redo-файле (512, 1024, 4096 байт). 

Каждый файл журнала онлайн-повтора, как и другие файлы Oracle, разбит на блоки. 
Размер блока, используемый данным файлом, находится с 21 байта в файле. 
В то время как администратор базы данных может установить размер блока для файлов данных, что касается онлайн-журналов повторных операций, размер блока фиксирован и меняется от операционной системы к операционной системе. 
Например, в Windows, Linux и Solaris размер блока `block_size` равен 512 байтам – 0x0200, тогда как в HP-UX размер блока равен 1024. 
Заголовок файла занимает весь один блок, как и заголовок журнала повторных операций, что означает, что два блока предназначены для информации о журнале повторных операций.

Через 25 байт в заголовке файла мы можем найти количество блоков в файле, не включая блок, используемый
самим заголовком файла. В показанном журнале повторных операций это число равно 0x00000043, или 67 в десятичной системе счисления. 
Посмотрев на это число и добавив 1 (для блока, используемого заголовком файла), а затем умножив его на
размер блока, мы сможем получить общий размер файла:

$$ (67 + 1) * 512 = 34816 \simeq 34,8Кб $$

"Магическое число" `magic_number` - это просто маркер файла, который используется Oracle как средство быстрой проверки того, действительно ли файл является файлом Oracle. Однако его можно использовать для идентификации порядка чтения/записи байтов:
* 0x7D7C7B7A - little-endian (default) 
* 0x7A7B7C7D - big-endian

### Стандартный заголовок блока

![](https://github.com/Igor743646/OpenLogReplicator/blob/master/documentation/ru/log-structure/images/block_header.png?raw=true)

Структура:

```rust
struct StandartBlockHeader {
    u8 block_flag;
    u8 type_of_file;
    padding[2];
    RBA rba;
    u16 checksum;
};
```

Каждый блок имеет свой собственный 16-байтовый заголовок, даже если запись повтора находится за пределами блока. 
Это важно учитывать при извлечении информации в виде символьной строки в журнал повтора, или любых других данных, которые могут быть разделены заголовком.

Ниже мы можем видеть пример заголовка блока. Заголовок каждого блока начинается с подписи `block_flag` (0x01) и `type_of_file` (0x22).
Далее следует номер блока, после чего мы можем найти порядковый номер, а затем смещение.

#### Relative Block Address (RBA)

```rust
struct RBA {
    u32 block_number;
    u32 sequance_number;
    u16 offset;
};
```

Последние три числа формируют относительный адрес блока (Relative Block Address) или RBA записи повтора со смещением, указывающим на начало записи внутри блока. Таким образом, если смещение равно 0x10, то redo запись начнется с `0x00000400 + 0x10 = 0x00000410`. Если же смещение равно 0, то это означает, что новой записи в блоке нет.
Наконец, есть контрольная сумма `checksum` данного блока.

#### Контрольная сумма блока

Для обеспечения целостности данных каждый блок имеет контрольную сумму. При проверке контрольной суммы,
каждый блок сначала интерпретируется как массив 16 байтных чисел (подблоки). Все они побитово выполняют операцию XOR.
Получается в итоге 128 битное число.

Далее это 16 байтное число разбивается на части по 4 байта, и они также выполняют операцию XOR. В конце старшие 2 байта ксоряться с младшими 2 байтами. В итоге должны получить 0, если контрольная сумма была записана правильно.

Другой алгоритм вычислить контрольную сумму проще. Блок разбивается на 64-битные числа, которые аккумулируется операцией XOR.
Далее выполняется XOR младших и старших 32 бит, затем младших и старших 16 бит соответственно.
Получается 2 байтное число, которое должно равняться 0, если данные целостны.

```rust
fn calcChSum(ref StandartBlock block) {
    u128 begin  = addressof(block);
    
    u64 res = 0;
    
    for (u64 i = 0, i < 512, i = i + 8) {
        u64 b1 = std::mem::read_unsigned(begin + i, 8, endian);
        res = res ^ b1;
    }
    
    res ^= res >> 32;
    res ^= res >> 16;
    res ^= block.header.checksum;
    
    return res & 0xFFFF;
};
```

### Блок 2 (Заголовок redo log файла)

![](https://github.com/Igor743646/OpenLogReplicator/blob/master/documentation/ru/log-structure/images/block2.png?raw=true)

Структура:

```rust
struct Block2 {
    StandartBlockHeader header [[hidden]];
    padding[4];
    u32 oracle_version;
    u32 database_id;
    char database_name[8];
    u32 control_sequance;
    u32 file_size;
    padding[4];
    u16 file_number;
    padding[2];
    u32 activation_id;
    padding[36];
    char description[64];
    u32 blocks_count;
    Time resetlogs_id;
    Scn resetlogs_scn;
    u32 hws;
    u16 thread;
    padding[2];
    Scn first_scn;
    Time first_time;
    Scn next_scn;
    Time next_time;
    u8 eot;
    u8 dis;
    u8 zero_blocks;
    u8 format_id;
    Scn enabled_scn;
    Time enabled_time;
    Scn thread_closed_scn;
    Time thread_closed_time;
    padding[4];
    u32 misc_flags;
    Scn terminal_recovery_scn;
    Time terminal_recovery_time;
    padding[8];
    Scn most_recent_scn;
    u32 largest_lwn;
    Scn real_next_scn;
    u32 standby_apply_delay;
    Scn prev_resetlogs_scn;
    Time prev_resetlogs_id;
    u32 misc_flags_2;
    padding[4];
    Time standby_log_close_time;
    padding[124];
    s32 thr;
    s32 seq2;
    Scn scn2;
    u8 redo_log_key[16];
    padding[16];
    u16 redo_log_key_flag;
    padding[30];
};
```

Заголовок журнала повтора гораздо интереснее заголовка файла и содержит большое количество
информации, такой как идентификатор базы данных, версия базы данных и время начала ведения журнала.

#### System Change Number (SCN)

Тип данных Scn представляет из себя 6 байтное число, представляемое 8 байтным в redo логе и состоит из 2 частей:
* base - 4 байта
* wrap - 2 байта

Также указывается, что начиная с версии 12c Oracle SCN имеет размер 8 байт - wrap часть увеличивается до 4 байт.

```rust
struct Scn {
    u8 data[8];
} [[format("ScnFormat")]];

fn ScnFormatLittle(Scn scn) {
    if (scn.data[0] == 0xFF && scn.data[1] == 0xFF && 
        scn.data[2] == 0xFF && scn.data[3] == 0xFF && 
        scn.data[4] == 0xFF && scn.data[5] == 0xFF)
        return 0xFFFFFFFFFFFFFFFF;
        
    u64 res = 0;
    res |= scn.data[0];
    res |= scn.data[1] << 8;
    res |= scn.data[2] << 16;
    res |= scn.data[3] << 24;
    if ((scn.data[5] & 0x80) == 0x80) {
        res |= scn.data[6] << 32;
        res |= scn.data[7] << 40;
        res |= scn.data[4] << 48;
        res |= (scn.data[5] & 0x7F) << 56;
    } else {
        res |= scn.data[4] << 32;
        res |= scn.data[5] << 40;
    }
    
    return res;
};

fn ScnFormatBig(Scn scn) {
    if (scn.data[0] == 0xFF && scn.data[1] == 0xFF && 
        scn.data[2] == 0xFF && scn.data[3] == 0xFF && 
        scn.data[4] == 0xFF && scn.data[5] == 0xFF)
        return 0xFFFFFFFFFFFFFFFF;
        
    u64 res = 0;
    res |= scn.data[3];
    res |= scn.data[2] << 8;
    res |= scn.data[1] << 16;
    res |= scn.data[0] << 24;
    if ((scn.data[4] & 0x80) == 0x80) {
        res |= scn.data[7] << 32;
        res |= scn.data[6] << 40;
        res |= scn.data[5] << 48;
        res |= (scn.data[4] & 0x7F) << 56;
    } else {
        res |= scn.data[5] << 32;
        res |= scn.data[4] << 40;
    }
    
    return res;
};

fn ScnFormat(Scn scn) {
    return endian == std::mem::Endian::Little ? ScnFormatLittle(scn) : ScnFormatBig(scn);
};
```

К примеру возьмём запись `D1 4F 1D 00 00 80 00 00`. Первые 4 байта являются base частью SCN.
В десятичном представлении это будет `0x001D4FD1 = 1920977`. 
По 5ому или 6ому байту, в зависимости от порядка записи байт, можно определить размер wrap части.
Если старший бит установлен в единицу, то wrap занимает 4 байта (не считая 1 бит), иначе 2 байта
(не считая 1 бит). 

В первом случае (который указан в примере) 2 следующих байта составляют старшую часть wrap SCN (`00 80`), отключая в 6 байте бит флага: `0x80 & 0x7F = 0x00`. Следующие 2 байта (`00 00`) составляют младшую часть wrap SCN.
Таким образом получается: `0x00000000.001D4FD1` или `1920977`.

Для примера `D1 4F 1D 01 AA D6 BB CC` результат будет таким: `0x56AACCBB.011D4FD1` (SCN: 6_245_028_936_852_262_865), 
т.к. `0xD6 & 0x7F = 0x56`.

А для `D1 4F 1D 01 AA 67 00 00` результат будет таким: `0x67AA.011D4FD1` (SCN: 113_979_860_799_441),
т.к. бит 6ого байта установлен в 0.

#### Time

Каждая запись о повторении в журналах имеет соответствующую временную метку, измеряемую в секундах, которая может быть использована для построения временной шкалы. Однако важно отметить, что временная метка указывает на момент записи в журнал, а не на момент возникновения события, что означает, что между возникновением события и его записью на диск может быть задержка. Например, если у пользователя отключена автоматическая фиксация на его клиенте (например, sqlplus), и он выполняет ВСТАВКУ через ноль секунд после минуты , а фиксацию - через тридцать секунд после минуты, то отметка времени будет указывать на последнее время. 

Хотя временные метки точно фиксируют время, когда запись повтора была записана на диск, точность временных меток, используемых для построения временной шкалы для операций DML, не следует рассматривать как “отлитую из железа”. Подтверждающие данные должны помочь устранить возможные различия во времени между возникновением события и его записью в журнал. С другой стороны, операции DDL “фиксируются” мгновенно.

```rust
struct Time {
    u32 t;
} [[format("TimeFormat")]];

fn TimeFormat(Time time) {
    u64 res = time.t;
    u64 ss = res % 60;
    res /= 60;
    u64 mi = res % 60;
    res /= 60;
    u64 hh = res % 24;
    res /= 24;
    u64 dd = (res % 31) + 1;
    res /= 31;
    u64 mm = (res % 12) + 1;
    res /= 12;
    u64 yy = res + 1988;
    return std::format("{}-{}-{} {}:{}:{} ({})", yy, mm, dd, hh, mi, ss, time.t);
};
```

Время представляется 32 байтным числом и отсчитывается с 1 января 1988 года, что позволяет использовать его до 18 августа 2121 года.

Самые важные поля:

| Поле | Описание |
|------|----------|
| `oracle_version` | Версия Oracle BD. Представляется 4 байтным числом. Формат: `major.maintenance.middleware.component-specific` по байту на каждый компонент. <br> В примере версия `0x13000000` обозначает Oracle 19.0.0.0. |
| `database_id` | Database ID - идентификатор базы данных. |
| `database_name` | Имя базы данных. Может быть не более 8 байт. |
| `activation_id` | Activation ID - Номер, присвоенный экземпляру базы данных. |
| `blocks_count` | Количество всех блоков в файле, включая заголовок файла. |
| `resetlogs_id` | Resetlogs ID - Идентификатор resetlogs, связанный с архивированным журналом повторных операций. Время resetlogs в базе данных, когда лог был записан. |
| `first_scn` | SCN первого изменения в архивном журнале. |
| `first_time` | Временная метка записи `first_scn`. |
| `next_scn` | SCN первого изменения в следующем архивном журнале. |
| `next_time` | Временная метка записи `next_scn`. |

## Redo Record

Структура:

```rust
struct Record {
    RecordHeader record_header;
    Char data[record_header.record_size - sizeof(record_header)];
};
```

Запись повтора, также известная как Redo Entry (Redo Record), содержит все изменения для данного SCN. Запись имеет
заголовок и один или несколько “векторов изменений”. Для данного события может быть один или несколько векторов изменений. 
Например, если пользователь выполняет INSERT в таблицу с индексом, то создается несколько
векторов изменений. Для вставки будет создан вектор повтора и отмены, а затем будет вставлена конечная строка
для индекса и фиксации. Каждый вектор изменений имеет свой собственный операционный код, который можно использовать для
различения векторов изменений.

### Заголовок (Redo Record)

![](https://github.com/Igor743646/OpenLogReplicator/blob/master/documentation/ru/log-structure/images/record_header.png?raw=true)

Структура:

```rust
struct RecordHeader {
    u32 record_size;
    u8 vld;
    padding[1];
    RecordSCN scn;
    u16 sub_scn;
    padding[2];
    
    if (VERSION >= 0x0C100000) {
        u32 container_uid;
        padding[4];
    } else {
        padding[8];
    }
        
    if (vld & 0x4) {
        u16 record_num;
        u16 record_num_max;
        u32 records_count;
        padding[8];
        Scn records_scn;
        Scn scn1;
        Scn scn2;
        Time records_timestamp;
    }
};
```

Заголовок записи повтора может быть 68 или 24 байта. Его размер определяется флагом в байте `vld`, согласно описанию структуры.
Заголовок в 68 байт определяет новый блок записей повтора, соответствующих новому SCN.

Первые 4 байта заголовка `record_size` определяют размер записи повтора, включая сам заголовок и запись размера. **Однако тут не учитываются размеры заголовков блоков**. Если запись повтора размера $N$ пересекает начало двух блоков, значит всего нам придётся прочитать $N + 16 \cdot 2$ байт. 

По 6 байту от заголовка блока записывается 6-байтный `scn` (что он означет не известно), а за ним `sub_scn`, обозначающий номер записи повтора, относящегося к текущему блоку.

Если 3 бит `vld` справа проставлен в 1 ($\text{vld} \wedge 0\text{x}4 \neq 0$), то это означает начало нового блока записей повтора. Тогда в заголовке указываются 2 числа:
`record_num` и `record_num_max`, предназначение которых неизвестно. Далее идёт `records_count`, указывающий количество записей повтора в блоке.
Следующие 4 байта неизвестны, однако на практике копируют количество записей в блоке. Далее ещё через 4 байта идёт 3 записи SCN, первая из которых идентифицируется, как SCN записи повтора. В конце записывается timestamp блока.

## Change Vector

Как упомяналось выше, записи повтора хранят один или несколько векторов изменений, каждому из которых соответствует некоторый операционный код.
Каждый вектор начинается с заголовка. Однако прежде стоит рассмотреть, что такое коды операций:

### Operation code

```rust
struct OpCode {
    u8 layer_code;
    u8 sub_code;
};
```

Первым `layer_code` и вторым `sub_code` байтами заголовка вектора изменений являются кодом операции. Всего их комбинаций не менее 300, однако для восстановления DML операций достаточно знать лишь ограниченное количество.

| Layer | Sub | Описание |
|-------:|-----|----------|
| Transaction Undo Management | |  |
| 5     | 2   | Update table in undo segment header (transaction begin) |
| 5     | 4   | Update transaction table (commit/rollback transaction) |
| 5     | 1   | Update table in undo segment block |
| 5     | 6   | Mark undo record "user undo done" (rollback) |
| Row access | | |
| 11    | 2   | Insert single row |
| 11    | 3   | Delete single row |
| 11    | 4   | Lock row |
| 11    | 5   | Update row |
| 11    | 11  | Insert multiple rows |
| 11    | 12  | Delete multiple rows |
| ... | | |


Каждая DML операция генерирует некоторую последовательность векторов изменений:

| | Statements |  | OpCode list |
|-|------------|--|-------------:|
| Insert single row ||||
| | -- Statement#1<br> INSERT INTO t1 VALUES (1); | HEADER <br> UNDO#1 <br> REDO#1 |  5.2 <br > 5.1 <br> 11.2 |
| | -- Statement#2<br> INSERT INTO t1 VALUES (2); | UNDO#2 <br> REDO#2 | 5.1 <br> 11.2 |
| | COMMIT; | COMMIT | 5.4 |
| Insert multiple rows ||||
| | -- Statement#1<br> INSERT INTO t1 <br> SELECT * FROM t2; | HEADER <br> UNDO#1 <br> REDO#1 |  5.2 <br > 5.1 <br> 11.11 |
| | COMMIT; | COMMIT | 5.4 |
| Update single row ||||
| | -- Statement#1<br> UPDATE t1 SET c2 = c2 + 1 <br> WHERE c1 = 1; | HEADER <br> UNDO#1 <br> REDO#1 |  5.2 <br > 5.1 <br> 11.5 |
| | -- Statement#2<br> UPDATE t1 SET c2 = c2 + 1 <br> WHERE c1 = 2; | UNDO#2 <br> REDO#2 | 5.1 <br> 11.5 |
| | COMMIT; | COMMIT | 5.4 |
| Update multiple rows ||||
| | -- T1 contains 2 rows <br> UPDATE t1 SET c2 = c2 + 1; | HEADER <br> UNDO#1 <br> REDO#1 <br> UNDO#2 <br> REDO#2 |  5.2 <br > 5.1 <br> 11.5 <br > 5.1 <br> 11.5 |
| | COMMIT; | COMMIT | 5.4 |
| Delete single row ||||
| | -- Statement#1<br> DELETE FROM t1 WHERE c1 = 1 | HEADER <br> UNDO#1 <br> REDO#1 |  5.2 <br > 5.1 <br> 11.3 |
| | -- Statement#2<br> DELETE FROM t1 WHERE c1 = 2 | UNDO#2 <br> REDO#2 | 5.1 <br> 11.3 |
| | COMMIT; | COMMIT | 5.4 |
| Delete multiple rows ||||
| | -- T1 contains 2 rows <br> DELETE FROM t1; | HEADER <br> UNDO#1 <br> REDO#1 <br> UNDO#2 <br> REDO#2 |  5.2 <br > 5.1 <br> 11.3 <br > 5.1 <br> 11.3 |
| | COMMIT; | COMMIT | 5.4 |
| Select for update single row ||||
| | -- Statement #1 <br> SELECT c2 FROM t1 <br> WHERE c1 = 1 <br> FOR UPDATE; | HEADER <br> UNDO#1 <br> REDO#1 |  5.2 <br > 5.1 <br> 11.4 |
| | -- Statement#2<br> UPDATE t1 SET c2 = c2 + 1 <br> WHERE c1 = 2; | UNDO#2 <br> REDO#2 | 5.1 <br> 11.5 |
| | COMMIT; | COMMIT | 5.4 |
| Select for update multiple rows ||||
| | -- T1 contains 2 rows <br> SELECT c2 FROM t1 <br> FOR UPDATE; | HEADER <br> UNDO#1 <br> REDO#1 <br> UNDO#2 <br> REDO#2 |  5.2 <br > 5.1 <br> 11.4 <br > 5.1 <br> 11.4 |
| | COMMIT; | COMMIT | 5.4 |
| Insert single row (rollbacked) ||||
| | -- Statement#1<br> INSERT INTO t1 VALUES (1); | HEADER <br> UNDO#1 <br> REDO#1 |  5.2 <br > 5.1 <br> 11.2 |
| | -- Statement#2<br> INSERT INTO t1 VALUES (2); | UNDO#2 <br> REDO#2 | 5.1 <br> 11.2 |
| | ROLLBACK; | UNDO#2 <br> REDO#2 <br> UNDO#2 <br> REDO#2 <br> COMMIT | 11.3 <br> 5.6 <br> 11.3 <br> 5.6 <br> 5.4 |

### Change Vector Header

![](https://github.com/Igor743646/OpenLogReplicator/blob/master/documentation/ru/log-structure/images/vector_header.png?raw=true)

Структура:

```rust
struct VectorHeader {
    OpCode operation_code;
    u16 class;
    u16 absolute_file_number;
    padding[2];
    Dba dba;
    Scn vector_scn;
    u8 sequence_number;
    u8 change_type;
    padding[2];
    
    if (VERSION >= 0x0C100000) {
        u16 container_id;
        padding[2];
        u16 flag;
        padding[2];
    }
    
    u16 fields_count [[format_read("ReadFieldsCount")]];
    u16 fields_sizes[(fields_count - 2) / 2];
};
```

Первые два байта, как говорилось выше, отводятся под код операции. Далее идет запись `class`, которая
судя по источникам, является номером класса блока (как в таблице V$BH.CLASS):

| Class | Description | Class | Description |
|-------|-------------|-------|-------------|
| 1 | data block                                | 10 | 3rd level bitmap block                   |
| 2 | sort block                                | 11 | bitmap block                             |
| 3 | deferred undo segment block               | 12 | bitmap index block                       |
| 4 | segment header block (table)              | 13 | file header block                        |
| 5 | deferred undo segment header              | 14 | unused                                   |
| 6 | free list blocks                          | 15 | system undo segment header               |
| 7 | extent map blocks                         | 16 | system undo segment block                |
| 8 | 1st level bitmap block                    | 15 + 2r | segment header for undo segment r   |
| 9 | 2nd level bitmap block                    | 16 + 2r | data blocks for undo segment r      |


С помощью данного поля мы можем определить номер сегмента отмены (Undo Segment Number - USN), 
если `class` >= 15:

$$\text{USN} = \frac{\text{class} - 15}{2}$$

Следующее поле `absolute_file_number` указывается на номер физического файла, присвоенный Oracle'ом.

Далее идёт 4-байтное поле `dba` (database block address), содержащее относительный номер файла и номер блока.

```rust
bitfield Dba {
    block_number : 22;
    relative_file_number : 10;
};
```

Тут старшие 22 бита отводятся под номер блока, а младшие 10 бит под относительные номер файла.

Затем пишется SCN ветора изменения `vector_scn`. Далее два байта `sequence_number` и `change_type`.
Если версия Oracle >= 12.1, то в заголовок по 24 байту также добавляется информация об id контейнера `container_id` и по 28 байту некий флаг `flag`. Таким образом заголовок составляет 24 байта, а в версии >= 12.1 - 32 байта.

После заголовка записываются `fields_count` - количество полей в данном векторе измения и последовательным массивом `fields_sizes` размеров каждого поля. На самом деле `fields_count` указывает на
размер в байтах информации о размерах полей вектора изменения, включая их количество. Таким образом, количество полей вычисляется по формуле:

$$\text{N} = \frac{\text{fields\_count} - 2}{2}$$

При этом сами поля будут начинаться с адреса кратного четырём, поэтому фактический размер массива с
размерами полей будет:

$$ \text{Len} = (\text{fields\_count} + 2) \wedge \text{0xFFFC} $$

В зависимости от этого в конце либо будет, либо не будет отступ в 2 байта.

## OpCode 5.2

![](https://github.com/Igor743646/OpenLogReplicator/blob/master/documentation/ru/log-structure/images/opcode0502.png?raw=true)

Структура:

```rust
struct ktudh {
    u16 xid_slot;
    padding[2];
    u32 xid_sequence;
    Uba uba;
    u16 flg;
    u16 siz;
    u8 fbi;
    padding[3];
    Xid pxid;
};

struct pdb {
    u32 pdb_id;
};

struct kteop {
    padding[4];
    u32 ext;
    padding[4];
    u32 ext_size;
    u32 highwater;
    padding[4];
    u32 offset;
    padding[8];
};

struct OpCode0502 {
    VectorHeader header;
    
    ktudh field1;
    
    if (VERSION < EVersion::ORACLE_12_1)
        break;
    
    if (((header.fields_count - 2) / 2) < 2)
        break;
    
    if (header.fields_sizes[1] == 4) {
        $ = ($ + 3) & 0xFFFC;
        pdb field2;
        break;
    }
    
    $ = ($ + 3) & 0xFFFC;
    kteop field2;
    
    if (((header.fields_count - 2) / 2) < 3) {
        break;
    }
    
    $ = ($ + 3) & 0xFFFC;
    pdb field3;   
};
```

Операционный код 5.2 состоит из одного, два или трёх полей. Для нас представляет интерес только первое поле `ktudh`, откуда мы
составляем XID по полям `xid_slot` и `xid_sequence` (при этом undo segment number мы берем из прочитанного в заголовке вектора
номера класса) и флаг `flg`. Больше никакой информации нам из вектора 5.2 не нужно.

Для версий выше 12.1 могут добавляться поля `pdb` (4 байта) и `kteop` (36 байт), но они интереса не представляют

## OpCode 5.1


