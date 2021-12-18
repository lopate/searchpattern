#include <sys/types.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <iostream>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <glob.h>
#include <time.h>
#include <dirent.h>
#include <algorithm>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>
#include <time.h>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <future>
#include <queue>
#include <mutex>

template<typename T>
struct pqueue{
    std::queue<T> _queue;
    std::mutex m;
    void push(const T& e){
        std::lock_guard<std::mutex> my_lock(m);
        _queue.push(e);
    }
    T& front(){
        std::lock_guard<std::mutex> my_lock(m);
        T ret = _queue.front();
        _queue.pop();
        return ret;
    }
    size_t size(){
        m.lock();
        return _queue.size();
    }
};

std::string getDir(){
	char buff[PATH_MAX];
	getcwd(buff, PATH_MAX);
	return std::string(buff);
}

struct SearchParam{
    bool onlyCurrentDir = false;
    size_t threadsCount = 1;
    std::string searchPattern = "";
    std::string path = "";
};

char parce(int argc, char* argv[], SearchParam& ret){
    bool gotPattern = false;
    ret.threadsCount = 1;
    ret.onlyCurrentDir = false;
    ret.searchPattern = "";
    ret.path = "";
    for(int i = 1; i < argc; i++){
        if(argv[i][0] == '-'){
            if(argv[i][1] == '\0'){
                write(2, "Неопознанный пустой аргумент -\n", 58);
                return 1;
            } else if(argv[i][1] == 'n'){
                ret.onlyCurrentDir = true;
            } else if(argv[i][1] == 't'){
                ret.threadsCount = atoll(&argv[i][2]);
            } else{
                write(2, "Неопознанный аргумент ", 43);
                write(2, argv[i], strlen(argv[i]));
                write(2, "\n", 2);
                return 1;
            }
        } else if(gotPattern){
            ret.path = std::string(argv[i]);
        } else{
            gotPattern = true;
            ret.searchPattern = std::string(argv[i]);
        }
    }
    if(!gotPattern){
        write(2, "Поиск по пустой строке невозможен\n", 63);
        return 1;
    }
    return 0;
}

size_t _getFileSize(int fd){
    size_t _file_size = 0;
	struct stat _fileStatbuff;
	if(fd == -1){
		_file_size = 0;
	}
	else{
		if ((fstat(fd, &_fileStatbuff) != 0) || (!S_ISREG(_fileStatbuff.st_mode))) {
			_file_size = 0;
		}
		else{
			_file_size = _fileStatbuff.st_size;
		}
	}
	return _file_size;
}

struct asyncSize_t {
    std::mutex m;
    size_t i;
};

struct nodeAutomata{
    char c;
    size_t pref;
};

size_t goAutomate(const std::vector<nodeAutomata>& automate, size_t node_index, char c){
    while(node_index && (node_index + 1 == automate.size() || automate[node_index + 1].c != c)){
        node_index = automate[node_index].pref;
    }
    if(automate[node_index + 1].c == c){
        node_index++;
    }
    return node_index;
}

size_t buildAutomate(std::vector<nodeAutomata>& automate, std::string pattern){
    automate.clear();
    automate.push_back({'\0', size_t(0)});
    for(auto& e: pattern){
        size_t node_index = automate.size() - 1;
        while(node_index && (node_index + 1 == automate.size() || automate[node_index + 1].c != e)){
            node_index = automate[node_index].pref;
        }
        if((node_index + 1 < automate.size()) && automate[node_index + 1].c == e){
            node_index++;
        }
        automate.push_back({e, node_index});
    }
    return 0;
}


struct find_pattern_ret_str_pair{
    size_t num;
    std::string ans_string;
};
struct find_pattern_ret{
    std::string path = "";
    std::vector<find_pattern_ret_str_pair> ret;
};

struct searchInFile_ret_pair{
    size_t strnum;
    std::string str;
    friend bool operator<(const searchInFile_ret_pair& a, const searchInFile_ret_pair& b){
        return a.strnum < b.strnum;
    }
};

struct searchInFile_ret{
    std::string path;
    std::vector<searchInFile_ret_pair> str;
};


void find_pattern(int fd, const SearchParam& comparam, const std::vector<nodeAutomata>& automate, std::vector<std::vector<find_pattern_ret>>& ret, size_t ret_i){
    
    size_t buf_size = size_t(2 << 20);
    size_t node_index = 0;
    bool getans = true;
    size_t strcnt = 0;
    std::string ans_string = "";
    ssize_t rdbites = 1;
    char* buf = (char*)malloc(buf_size * sizeof(char));
    while(rdbites){
        rdbites = read(fd, buf, buf_size);
        for(ssize_t i = 0; i < rdbites; i++){
            if(buf[i] != '\n'){
                ans_string += buf[i];
            }
            
            node_index = goAutomate(automate, node_index, buf[i]);
            if(getans && (node_index == (automate.size() - 1))){
                //printf("Found in [%d] at %d\n", offset, p + i);
                
                getans = false;
            }
            
            if(buf[i] == '\n'){
                if(!getans){
                    ret[ret_i][ret[ret_i].size() - 1].ret.push_back({strcnt, ans_string});
                }
                ans_string = "";
                strcnt++;
                
                getans = true;
            }
        }
    }
    free(buf);
    return;
}


void seak_pattern(const SearchParam& comparam, const std::vector<nodeAutomata>& automate,  std::vector<std::vector<find_pattern_ret>>& ret, size_t ret_i, pqueue<std::string>& queue, std::atomic_bool& cont){
    
    while(cont || queue._queue.size()){
        queue.m.lock();
        if(queue._queue.size()){
            std::string path = queue._queue.front();
            queue._queue.pop();
            queue.m.unlock();
            ret[ret_i].push_back({path, std::vector<find_pattern_ret_str_pair>()});
            int fd = open(path.c_str(), O_RDONLY);
            find_pattern(fd, comparam, automate, ret, ret_i);
            close(fd);
        }
        else{
            queue.m.unlock();
        }
    }
}


void searchInFile(std::string path, const SearchParam& comparam, pqueue<std::string>& queue){
    queue.push(path);
}

void searchInDir(std::string path, DIR* dir, const SearchParam& comparam,  pqueue<std::string>& queue){
    for (dirent *cdir = readdir(dir); cdir != nullptr; cdir = readdir(dir)){
        std::string nextname(path+ "/" + cdir->d_name);
        if (cdir->d_type == DT_REG){
            searchInFile(nextname, comparam, queue);
        }
        
    }
    rewinddir(dir);
    if(!comparam.onlyCurrentDir){
        for (dirent *cdir = readdir(dir); cdir != nullptr; cdir = readdir(dir)){
            std::string nextname(path+ "/" + cdir->d_name);
           
            if (cdir->d_type == DT_DIR && !(strcmp(cdir->d_name,".") == 0 || strcmp(cdir->d_name,"..") == 0)){
                DIR* nextdir = opendir(nextname.c_str());
                searchInDir(nextname, nextdir, comparam, queue);
                closedir(nextdir);
            }
            
        }
    }
}


int main(int argc, char* argv[]) {
    SearchParam comparam;
    size_t printed = 0;
    if(parce(argc, argv, comparam)){
        return 1;
    }
    DIR *dir = nullptr;
    std::string path;
    path = comparam.path;
    if(comparam.path.size() == 0){
        path = getDir();
    }
    
    dir = opendir(path.c_str());
    size_t threadsUsed = 0;
    std::vector<std::thread> curThreads;
    std::vector<nodeAutomata> automate;
    buildAutomate(automate, comparam.searchPattern);
    std::vector<int> fd;
    std::thread printret_thread;
    if(dir == nullptr){
        write(2, "Невозможно получить доступ к папке ", 66);
        write(2, comparam.path.c_str(), comparam.path.size());
        write(2, "\n", 2);
        return 1;
    }
    pqueue<std::string> queue;
    std::atomic_bool cont = true;
    std::vector<std::vector<find_pattern_ret>> ret(comparam.threadsCount);
    for(size_t t = 0; t < comparam.threadsCount; t++){
        curThreads.push_back(std::thread(seak_pattern, std::ref(comparam),  std::ref(automate), std::ref(ret), size_t(t), std::ref(queue), std::ref(cont)));
    }
    searchInDir(path, dir, comparam, queue);
    cont = false;
    for(size_t i = 0;i < curThreads.size(); i++){
        curThreads[i].join();
        for(auto& file:ret[i]){
            if(file.ret.size()){
                printf("Search pattern has been found in file: %s\n", file.path.c_str());
                for(auto& e:file.ret){
                    printf("\t in string [%d]: %s\n", e.num + 1, e.ans_string.c_str());
                }
            }
            
        }
        
    }
    closedir(dir);
	return 0;
}