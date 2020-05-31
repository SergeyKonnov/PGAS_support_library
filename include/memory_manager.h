#ifndef __MEMORY_MANAGER_H__
#define __MEMORY_MANAGER_H__

#include <map>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <iostream>

#define QUANTUM_SIZE 5

enum mods {  // используется для изменения режима работы с памятью
    READ_ONLY,
    READ_WRITE
};

enum tags {  // используется для корректного распределения пересылок данных через MPI
    GET_DATA_FROM_HELPER = 123,
    SEND_DATA_TO_HELPER  = 234,
    SEND_DATA_TO_MASTER_HELPER = 345,
    GET_DATA_FROM_MASTER_HELPER_LOCK = 456,
    GET_INFO_FROM_MASTER_HELPER = 567,
    GET_PERMISSION_FOR_CHANGE_MODE = 678
};

enum operations {  // используется вспомогательными потоками для определения типа запрашиваемой операции
    GET_DATA_RW,
    GET_DATA_R,
    SET_INFO,
    GET_INFO,
    LOCK,
    UNLOCK,
    CHANGE_MODE
};
void worker_helper_thread();
void master_helper_thread();

struct memory_line {  // память для одного parallel_vector
    // for workers
    std::vector<int*>quantums;  // вектор указателей на кванты
    int logical_size;  // общее число элементов в векторе на всех процессах
    std::vector<std::mutex*> mutexes;  // мьютексы на каждый квант, нужны, чтобы предотвратить одновременный доступ
                                       // к кванту с разных потоков в режиме READ_WRITE
    // for master
    std::vector<std::pair<bool, int>>quantum_owner; // для read_write mode, массив пар: bool - готов ли квант для передачи;
                                                                                    // int - на каком процессе расположен квант
    std::map<int, std::queue<int>> wait_locks;  // мапа очередей для процессов, ожидающих разблокировки кванта, заблокированных через set_lock
    std::map<int, std::queue<int>>wait_quantums;  // мапа очередей для процессов, ожидающих разблокировки кванта, заблокированных процессом-мастером
    std::vector<int> quantums_for_lock;  // вектор для определения номеров процессов, блокирующих кванты
    std::vector<std::vector<int>> owners; // для read_only mode, номера процессов, хранящих у себя квант
    // for master and workers
    std::vector<int> num_change_mode;  // для перехода между режимами
};

class memory_manager {
    std::vector<memory_line> memory;  // структура-хранилище памяти и вспомогательной информации
    std::thread helper_thr;  // вспомогательный поток
    int rank, size;  // ранг процесса в MPI и число процессов
    int worker_rank, worker_size;  // worker_rank = rank-1, worker_size = size-1
    bool is_read_only_mode;  // активен ли режим READ_ONLY
    int num_of_change_mode_procs;  // число процессов, обратившихся к мастеру для изменения режима работы
    int num_of_change_mode;  // общее число изменений режима работы
    std::vector<long long> times;  // вектор, сохраняющий информацию, когда в последний раз было обращение к какому-либо процессу
    long long time;  // вспомогательный счётчик для вектора times
public:
    void memory_manager_init(int argc, char** argv);  // функция, вызываемая в начале выполнения программы, инициирует вспомогательные потоки
    int get_MPI_rank();
    int get_MPI_size();
    int get_data(int key, int index_of_element);  // получить элемент по индексу с любого процесса
    void set_data(int key, int index_of_element, int value);  // сохранить значение элемента по индексу с любого процесса
    int create_object(int number_of_elements);  // создать новый memory_line и занести его в memory
    int get_quantum_index(int index);  // получить номер кванта по индексу
    void set_lock(int key, int quantum_index);  // заблокировать квант
    void unset_lock(int key, int quantum_index);  // разблокировать квант
    void change_mode(int mode);  // сменить режим работы с памятью
    void finalize();  // функция, завершающая выполнение программы, останавливает вспомогательные потоки
    friend void worker_helper_thread();  // функция, выполняемая вспомогательными потоками процессов-рабочих
    friend void master_helper_thread();  // функция, выполняемая вспомогательным потоком процесса-мастера
};

extern memory_manager mm;  // работа с менеджером памяти идёт через этот объект

#endif  // __MEMORY_MANAGER_H__

