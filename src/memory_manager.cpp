#include <thread>
#include <mpi.h>
#include <vector>
#include <iostream>
#include <cassert>
#include "memory_manager.h"

// посылка: [операция; номер структуры, откуда требуются данные; требуемый номер элемента, кому требуется переслать объект;
//           значение элемента(для get_data = -1)]
// если номер структуры = -1, то завершение функции helper_thread
memory_manager mm;

void memory_manager::memory_manager_init(int argc, char**argv) {
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if(provided != MPI_THREAD_MULTIPLE) {
            std::cout << "Error: MPI thread support insufficient! required " << MPI_THREAD_MULTIPLE << " provided " << provided;
            abort();
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    worker_rank = rank - 1;
    worker_size = size - 1;
    if(rank == 0) {
        helper_thr = std::thread(master_helper_thread);
    } else {
        helper_thr = std::thread(worker_helper_thread);
    }
    is_read_only_mode = false;
    // создание своего типа для пересылки посылок ???
}

int memory_manager::create_object(int number_of_elements) {
    memory_line line;
    line.logical_size = number_of_elements;
    if(rank == 0) {
        line.quantums_for_lock.resize((number_of_elements + QUANTUM_SIZE - 1) / QUANTUM_SIZE);
        line.quantum_owner.resize((number_of_elements + QUANTUM_SIZE - 1) / QUANTUM_SIZE);
        for(int i = 0; i < line.quantums_for_lock.size(); i++) {
            line.quantums_for_lock[i] = -1;
            line.quantum_owner[i] = -1;
        }
    } else {
        line.quantums.resize((number_of_elements + QUANTUM_SIZE - 1) / QUANTUM_SIZE);
        for(int i = 0; i < line.quantums.size(); i++) {
            line.quantums[i] = {false, nullptr};
        }
    }
    memory.push_back(line);
    MPI_Barrier(MPI_COMM_WORLD);
    return memory.size()-1;
}

// void memory_manager::delete_object(int* object) { 
//     auto tmp = map_pointer_to_int.find(object);
//     if(tmp == map_pointer_to_int.end())
//         throw -1;
//     auto tmp2 = map_int_to_pointer.find(tmp->second);  // ???
//     if(tmp2 == map_int_to_pointer.end())
//         throw -2;
//     map_int_to_pointer.erase(tmp2);
//     map_pointer_to_int.erase(tmp);
// }

int memory_manager::get_size_of_portion(int key) {
    return memory[key].vector_size;
}


int memory_manager::get_data(int key, int index_of_element) {
    // send to 0, чтобы узнать, какой процесс затребовал элемент?
    int to_rank = get_number_of_process(key, index_of_element);
    int num_elem = get_number_of_element(key, index_of_element);
    int num_quantum = get_quantum_index(num_elem);
    if(rank == to_rank)
        return memory[key].vector[num_elem];
    int request[] = {GET_DATA, key, num_quantum, -1};
    if(is_in_buffer(key, index_of_element))
        return memory[key].buffer[index_of_element%QUANTUM_SIZE];
    MPI_Send(request, 4, MPI_INT, to_rank, SEND_DATA_TO_HELPER, MPI_COMM_WORLD);
    MPI_Status status;
    MPI_Recv(mm.memory[key].buffer, QUANTUM_SIZE, MPI_INT, to_rank, GET_DATA_FROM_HELPER, MPI_COMM_WORLD, &status);
    mm.memory[key].index_buffer = mm.get_quantum_index(index_of_element)*QUANTUM_SIZE;
    return mm.memory[key].buffer[index_of_element - mm.memory[key].index_buffer];
}

void memory_manager::set_data(int key, int index_of_element, int value) {
    if(is_read_only_mode) {
        throw -1;
    }
    int to_rank = get_number_of_process(key, index_of_element);
    int num_elem = get_number_of_element(key, index_of_element);
    if(rank == to_rank)
        memory[key].vector[num_elem] = value;
    int request[] = {SET_DATA, key, num_elem, value};
    MPI_Send(request, 4, MPI_INT, to_rank, SEND_DATA_TO_HELPER, MPI_COMM_WORLD);
    int tmp;
    MPI_Status status;
    MPI_Recv(&tmp, 1, MPI_INT, to_rank, GET_DATA_FROM_HELPER, MPI_COMM_WORLD, &status);
}

void memory_manager::copy_data(int key_from, int key_to) {
    memory[key_to] = memory[key_from];
}

int memory_manager::get_data_by_index_on_process(int key, int index) {
    return memory[key].vector[index];
}

void memory_manager::set_data_by_index_on_process(int key, int index, int value) {
    if(is_read_only_mode)
        throw -1;
    memory[key].vector[index] = value;
}

int memory_manager::get_number_of_process(int key, int index) {
    int number_proc;
    int quantum_index = get_quantum_index(index);
    int tmp1 = int((memory[key].logical_size + QUANTUM_SIZE - 1)/QUANTUM_SIZE)%worker_size;
    int tmp2 = int((memory[key].logical_size + QUANTUM_SIZE - 1)/QUANTUM_SIZE)/worker_size;
    if(quantum_index < tmp1*(tmp2+1)) {
        number_proc = quantum_index/(tmp2+1);
    } else {
        int tmp = quantum_index - tmp1*(tmp2+1);
        number_proc = tmp1 + tmp/tmp2;
    }
    number_proc += 1;
    return number_proc;
}

int memory_manager::get_number_of_element(int key, int index) {
    int number_elem, quantum_number_elem;
    int quantum_index = get_quantum_index(index);
    int tmp1 = int((memory[key].logical_size + QUANTUM_SIZE - 1)/QUANTUM_SIZE)%worker_size;
    int tmp2 = int((memory[key].logical_size + QUANTUM_SIZE - 1)/QUANTUM_SIZE)/worker_size;
    if(quantum_index < tmp1*(tmp2+1)) {
        quantum_number_elem = quantum_index%(tmp2+1);
    } else {
        int tmp = quantum_index - tmp1*(tmp2+1);
        quantum_number_elem = tmp%tmp2;
    }
    number_elem = quantum_number_elem*QUANTUM_SIZE + index%QUANTUM_SIZE;
    return number_elem;
}

int memory_manager::get_global_index_of_element(int key, int index, int process) {
    if(process == 0)
        throw -2;
    int number_elem;
    process -= 1;
    int num_of_quantums = (memory[key].logical_size + QUANTUM_SIZE-1)/QUANTUM_SIZE;
    if(process < num_of_quantums%worker_size) {
        number_elem = process*(num_of_quantums/worker_size+1)*QUANTUM_SIZE+index;
    } else {
        number_elem = (num_of_quantums/worker_size+1)*QUANTUM_SIZE*(num_of_quantums%worker_size) +
                                    + (process-num_of_quantums%worker_size)*(num_of_quantums/worker_size)*QUANTUM_SIZE + index;
    }
    return number_elem;
}

int memory_manager::get_quantum_index(int index) {
    return index/QUANTUM_SIZE;
}


void worker_helper_thread() {
    int request[4];
    MPI_Status status;
    int rank, size;
    int cnt_end = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    while(true) {
        MPI_Recv(request, 4, MPI_INT, MPI_ANY_SOURCE, SEND_DATA_TO_HELPER, MPI_COMM_WORLD, &status);
        if(request[0] == -1) {
            cnt_end++;
            if(cnt_end == size)
                break;
        }
        if(request[0] == GET_DATA) {
            int to_rank = status.MPI_SOURCE;
            int key = request[1], index = request[2];
            if(key < 0 || key >= mm.memory.size())
                throw -1;
            int* data = mm.memory[key].vector.data() + index*QUANTUM_SIZE;
            MPI_Send(data, QUANTUM_SIZE, MPI_INT, to_rank, GET_DATA_FROM_HELPER, MPI_COMM_WORLD);
        }
        else if(request[0] == SET_DATA) {
            int key = request[1], index = request[2], value = request[3];
            if(key < 0 || key >= mm.memory.size())
                throw -1;
            if(index < 0 || index >= mm.memory[key].vector.size())
                throw "-2";
            mm.memory[key].vector[index] = value;
            int to_rank = status.MPI_SOURCE;
            int tmp = 1;
            MPI_Send(&tmp, 1, MPI_INT, to_rank, GET_DATA_FROM_HELPER, MPI_COMM_WORLD);
        }
    }
}

void master_helper_thread() {
    int request[3];
    MPI_Status status;
    int rank, size;
    int cnt_end = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    while(true) {
        MPI_Recv(&request, 3, MPI_INT, MPI_ANY_SOURCE, SEND_DATA_TO_MASTER_HELPER, MPI_COMM_WORLD, &status);
        if(request[0] == -1) {
            cnt_end++;
            if(cnt_end == size)
                break;
        }
        int key = request[1], quantum = request[2];
        if(request[0] == LOCK_READ || request[0] == LOCK_WRITE) {
            if(request[0] == LOCK_WRITE) {

            }
            if(mm.memory[key].quantums_for_lock[quantum] == -1) {
                int to_rank = status.MPI_SOURCE;
                int tmp = 1;
                mm.memory[key].quantums_for_lock[quantum] = status.MPI_SOURCE;
                MPI_Send(&tmp, 1, MPI_INT, to_rank, GET_DATA_FROM_MASTER_HELPER, MPI_COMM_WORLD);
            } else {
                if(mm.memory[key].wait.find(quantum) == mm.memory[key].wait.end())
                    mm.memory[key].wait.insert({quantum, std::queue<int>{}});
                mm.memory[key].wait[quantum].push(status.MPI_SOURCE);
            }
        }
        else if(request[0] == UNLOCK) {
            if(mm.memory[key].quantums_for_lock[quantum] == status.MPI_SOURCE) {
                mm.memory[key].quantums_for_lock[quantum] = -1;
                if(mm.memory[key].wait.find(quantum) != mm.memory[key].wait.end()) {
                    int to_rank = mm.memory[key].wait[quantum].front();
                    mm.memory[key].wait[quantum].pop();
                    mm.memory[key].quantums_for_lock[quantum] = to_rank;
                    if(mm.memory[key].wait[quantum].size() == 0)
                        mm.memory[key].wait.erase(quantum);
                    int tmp = 1;
                    MPI_Send(&tmp, 1, MPI_INT, to_rank, GET_DATA_FROM_MASTER_HELPER, MPI_COMM_WORLD);
                }
            }
        }
    }
}

void memory_manager::set_lock_read(int key, int quantum_index) {
    int request[] = {LOCK_READ, key, quantum_index};
    MPI_Send(request, 3, MPI_INT, 0, SEND_DATA_TO_MASTER_HELPER, MPI_COMM_WORLD);
    int ans;
    MPI_Status status;
    MPI_Recv(&ans, 1, MPI_INT, 0, GET_DATA_FROM_MASTER_HELPER, MPI_COMM_WORLD, &status);
}

void memory_manager::set_lock_write(int key, int quantum_index) {
    // int request[] = {LOCK_WRITE, key, quantum_index};
    // MPI_Send(request, 3, MPI_INT, 0, SEND_DATA_TO_MASTER_HELPER, MPI_COMM_WORLD);
    // int ans;
    // MPI_Status status;
    // MPI_Recv(&ans, 1, MPI_INT, 0, GET_DATA_FROM_MASTER_HELPER, MPI_COMM_WORLD, &status);
}

void memory_manager::unset_lock(int key, int quantum_index) {
    int request[3] = {UNLOCK, key, quantum_index};
    MPI_Send(request, 3, MPI_INT, 0, SEND_DATA_TO_MASTER_HELPER, MPI_COMM_WORLD);
}

bool memory_manager::is_in_buffer(int key, int logical_index) {
    return is_read_only_mode && (logical_index >= memory[key].index_buffer && logical_index < memory[key].index_buffer + QUANTUM_SIZE);
}

void memory_manager::finalize() {
    MPI_Barrier(MPI_COMM_WORLD);
    int request[4] = {-1, -1, -1, -1};
    MPI_Send(request, 3, MPI_INT, 0, SEND_DATA_TO_MASTER_HELPER, MPI_COMM_WORLD);
    for(int i = 1; i < size; i++) {
        MPI_Send(request, 4, MPI_INT, i, SEND_DATA_TO_HELPER, MPI_COMM_WORLD);
    }
    assert(helper_thr.joinable());
    helper_thr.join();
    
    MPI_Finalize();
}

void memory_manager::section_lock(int mode) {
    if(mode == READ_ONLY) {
        is_read_only_mode = true;
    } else if(mode == READ_WRITE) {
        
    }
}

void memory_manager::section_unlock(int mode) {
    if(mode == READ_ONLY) {
        is_read_only_mode = false;
    } else if(mode == READ_WRITE) {
        
    }
}

// memory_manager::~memory_manager() {
    // int request[2] = {-1, -1};
    // if(rank == 0) {
    //     for(int i = 0; i < size; i++) {
    //         std::cout<<"MPI_Send to rank "<<rank<<" from destructor\n";
    //         MPI_Send(request, 2, MPI_INT, 0, SEND_DATA_TO_HELPER, MPI_COMM_WORLD);
    //     }
    // }
    // if(helper_thr.joinable())   
    //     helper_thr.join();
    // std::cout<<"join ended\n";
    // MPI_Finalize();
    // std::cout<<"this is destructor\n";
// }