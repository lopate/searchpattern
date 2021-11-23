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
    size_t str_offset;
    friend bool operator<(const find_pattern_ret_str_pair& a, const find_pattern_ret_str_pair& b){
        return a.num < b.num;
    }
};

struct find_pattern_ret{
    std::string path;
    std::vector<find_pattern_ret_str_pair> ret;
    size_t strcnt;
    size_t laststroffset = 0;
    bool getans = true;
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
void composeAns(std::vector<find_pattern_ret>& answers, std::vector<searchInFile_ret>& ret, const size_t printable){
    std::string path = "";
    size_t strcnt = 0;
    bool getans = true;
    bool npushed = true;
    for(size_t j = 0; j < printable; j++){
        if(path != answers[j].path){
            path = answers[j].path;
            getans = true;
            npushed = true;
            strcnt = 0;
        }
        for(size_t i = 0; i < answers[j].ret.size();i++){
            if(getans || answers[j].ret[i].num){
                if(npushed){
                    ret.push_back({path,  std::vector<searchInFile_ret_pair>(0)});
                    npushed = false;
                }
                
                int fd = open(answers[j].path.c_str(), O_RDONLY);
                if(fd == -1){
                    write(2, "Ошибка открытия файла ", 42);
                    write(2, answers[j].path.c_str(), answers[j].path.size());
                    write(2, "\n", 2);
                    return;
                }
                std::string ret_str = "";
                size_t buf_size = 4096;
                char* buf = (char*)malloc(buf_size * sizeof(char));
                ssize_t rdbites = 1;
                size_t p = answers[j].ret[i].str_offset;
                
                if((strcnt) && (answers[j].ret[i].num == 0)){
                    size_t q = j - 1;
                    while(q > 0 && answers[q].strcnt == 0){
                        q--;
                    };
                    p = answers[q].laststroffset;
                }
                
                bool read = true;
                while(read && (rdbites = pread64(fd, buf, buf_size, p))){
                    for(size_t j = 0; j < rdbites; j++){
                       if(buf[j] == '\n'){
                           read = false;
                           j = rdbites;
                       } else{
                           ret_str += buf[j];
                       }
                    }
                    p += rdbites;
                }
                close(fd);
                free(buf);
                ret[ret.size() - 1].str.push_back({answers[j].ret[i].num + strcnt, ret_str});
            }
            getans = true;
        }
        strcnt += answers[j].strcnt;
        getans = answers[j].getans;
    }
    for(auto& e: ret){
        for(size_t i = 0; i < e.str.size(); i++){
            sort(e.str.begin(), e.str.end());
        }
        std::vector<searchInFile_ret_pair> newstr;
        if(e.str.size() > 0){
            newstr.push_back(e.str[0]);
        }
        for(size_t j = 1; j < e.str.size();j++){
            if(e.str[j].strnum !=e.str[j-1].strnum){
                newstr.push_back(e.str[j]);
            }
        }
         e.str = newstr;
    }
}

void printret(std::vector<find_pattern_ret>& find_pattern_ret, const size_t printable){
    std::vector<searchInFile_ret> ret;
    composeAns(find_pattern_ret, ret, printable);
    for(size_t i = 0; i < ret.size();i++){
        printf("Search pattern has been found in file: %s\n", ret[i].path.c_str());
        for(int j = 0; j < ret[i].str.size(); j++){
            printf("\t in string[%d]: %s\n",(ret[i].str[j].strnum + 1),  ret[i].str[j].str.c_str());
        }
    }
}

/*void find_pattern(int fd, size_t offset, size_t blocksize, const SearchParam& comparam, const std::vector<nodeAutomata>& automate, std::vector<find_pattern_ret>& ret, size_t ret_i){
    size_t buf_size = std::min(size_t(2 << 20), blocksize);
    size_t node_index = 0;
    bool getans = true;
    size_t strcnt = 0;
    size_t ans_offset = offset;
    char* buf = (char*)malloc(buf_size * sizeof(char));
    ret[ret_i].laststroffset = offset;
    ssize_t rdbites = 1;
    for(ssize_t p = offset; rdbites && (p < offset + blocksize + comparam.searchPattern.size()); p += rdbites){
        rdbites = pread64(fd, buf, buf_size, p);
        for(ssize_t i = 0; i < rdbites; i++){
            node_index = goAutomate(automate, node_index, buf[i]);
            if(getans && (node_index == (automate.size() - 1))){
                //printf("Found in [%d] at %d\n", offset, p + i);
                ret[ret_i].ret.push_back({strcnt, ans_offset});
                getans = false;
            }
            if(buf[i] == '\n'){
                strcnt++;
                ans_offset = p + i + 1;
                if(p + i < offset + blocksize){
                    ret[ret_i].strcnt = strcnt;
                    ret[ret_i].laststroffset = ans_offset ;
                }
               
                getans = true;
            }
        }
    }
    free(buf);
    ret[ret_i].getans = getans;
    return;
}
*/
void find_pattern(int fd, size_t offset, size_t blocksize, const SearchParam& comparam, const std::vector<nodeAutomata>& automate, std::vector<find_pattern_ret>& ret, size_t ret_i){
    
    size_t buf_size = std::min(size_t(2 << 20), blocksize);
    size_t node_index = 0;
    bool getans = true;
    size_t strcnt = 0;
    size_t ans_offset = offset;
    char* buf = (char*)malloc(buf_size * sizeof(char));
    ret[ret_i].laststroffset = offset;
    ssize_t rdbites = 1;
    for(ssize_t p = offset; rdbites && (p < offset + blocksize + comparam.searchPattern.size()); p += rdbites){
        rdbites = pread64(fd, buf, buf_size, p);
        for(ssize_t i = 0; i < rdbites; i++){
            node_index = goAutomate(automate, node_index, buf[i]);
            if(getans && (node_index == (automate.size() - 1))){
                //printf("Found in [%d] at %d\n", offset, p + i);
                ret[ret_i].ret.push_back({strcnt, ans_offset});
                getans = false;
            }
            if(buf[i] == '\n'){
                strcnt++;
                ans_offset = p + i + 1;
                if(p + i < offset + blocksize){
                    ret[ret_i].strcnt = strcnt;
                    ret[ret_i].laststroffset = ans_offset ;
                }
               
                getans = true;
            }
        }
    }
    free(buf);
    ret[ret_i].getans = getans;
    return;
}
void searchInFile(std::string path, const SearchParam& comparam, size_t& threadsUsed, std::vector<std::thread>& curThreads, const std::vector<nodeAutomata>& automate, std::vector<find_pattern_ret>& ret,std::vector<int>& fd){
    fd.push_back(open(path.c_str(), O_RDONLY));
    if(fd[fd.size()-1] == -1){
        write(2, "Ошибка открытия файла ", 42);
        write(2, path.c_str(), path.size());
        write(2, "\n", 2);
        return;
    }
    size_t file_size = _getFileSize(fd[fd.size()-1]);
    size_t maxthread = file_size / comparam.searchPattern.size() - 1;
    
    if(file_size % comparam.searchPattern.size()){
        maxthread++;
    }
    maxthread  = std::min(maxthread, comparam.threadsCount);
    size_t blocksize = 0;
    if(file_size == 0){
        maxthread = 0;
    }
    if (maxthread != 0){
        blocksize = file_size / maxthread;
        if(file_size % maxthread){
            blocksize++;
        }
    }
    size_t printable = ret.size();
    for(size_t i = 0; i < maxthread; i++){
        threadsUsed++;
        ret.push_back(find_pattern_ret());
        ret[ret.size() - 1].path = path;
        //void find_pattern(int fd, size_t offset, size_t blocksize, const SearchParam& comparam, const std::vector<nodeAutomata>& automate, find_pattern_ret& ret)
        curThreads.push_back(std::thread(find_pattern, fd[fd.size() - 1], size_t(i * blocksize), blocksize, std::ref(comparam),  std::ref(automate), std::ref(ret), ret.size() - 1));
        if(i == maxthread - 1){
            printable += maxthread;
        }
        if(curThreads.size() == comparam.threadsCount){
            threadsUsed = 0;
            std::vector<find_pattern_ret> new_ret;
            std::vector<int> new_fd;
            for(auto& e: curThreads){
                e.join();
            }
            curThreads.resize(0);
            for(size_t j = 0; j < fd.size() - 1; j++){
                close(fd[j]);
            }
            //printret(ret, printable);
            if(i == maxthread - 1){
                close(fd[fd.size() - 1]);
            }else{
                new_fd.push_back(fd[fd.size() - 1]);
            }
            /*for(size_t j = printable; j < ret.size(); j++){
                new_ret.push_back(ret[j]);
            }
            
            ret = new_ret;*/
            fd = new_fd;
        }
    }
    
}

void searchInDir(std::string path, DIR* dir, const SearchParam& comparam, size_t& threadsUsed,std::vector<std::thread>& curThreads,const std::vector<nodeAutomata>& automate, std::vector<find_pattern_ret>& ret,std::vector<int>& fd){
    for (dirent *cdir = readdir(dir); cdir != nullptr; cdir = readdir(dir)){
        std::string nextname(path+ "/" + cdir->d_name);
        if (cdir->d_type == DT_REG){
            searchInFile(path+"/"+cdir->d_name, comparam, threadsUsed, curThreads, automate, ret, fd);
        }
        
    }
    if(!comparam.onlyCurrentDir){
        for (dirent *cdir = readdir(dir); cdir != nullptr; cdir = readdir(dir)){
            std::string nextname(path+ "/" + cdir->d_name);
            DIR* nextdir = opendir(path.c_str());
            if (cdir->d_type == DT_DIR){
                searchInDir(path.c_str(), nextdir, comparam, threadsUsed, curThreads, automate, ret, fd);
            }
        }
    }
}


int main(int argc, char* argv[]) {
    SearchParam comparam;
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
    std::vector<find_pattern_ret> ret;
    std::vector<int> fd;
    
    if(dir == nullptr){
        write(2, "Невозможно получить доступ к папке ", 66);
        write(2, comparam.path.c_str(), comparam.path.size());
        write(2, "\n", 2);
        return 1;
    }
    searchInDir(path, dir, comparam, threadsUsed,curThreads, automate, ret, fd);
    for(auto& e: curThreads){
        e.join();
    }
    
    printret(ret, ret.size());
    for(auto& e:fd){
        close(e);
    }
    closedir(dir);
	return 0;
}