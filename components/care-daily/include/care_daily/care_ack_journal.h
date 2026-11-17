#pragma once
#include <string>
#include <string_view>

namespace xiaozhi_care::daily {
// One NVS string per journal. Never silently evict a confirmed occurrence.
// Only a strictly newer local date starts a new daily journal.
class CareAckJournal {
public:
    static constexpr size_t kMaxBytes = 3500;
    static constexpr size_t kMaxKey = 95;

    static bool CanAccept(std::string_view journal, std::string_view date, std::string_view key) {
        if (!ValidDate(date) || !ValidKey(key,date) || journal.size() > kMaxBytes) return false;
        if (journal.empty()) return true;
        const auto saved_date = journal.substr(0,10);
        if (!ValidDate(saved_date) || journal.size() <= 11 || journal[10] != '\n' ||
            journal.back() != '\n' || date < saved_date) return false;
        size_t offset=11;
        while (offset < journal.size()) {
            const auto end=journal.find('\n',offset);
            if (end == std::string_view::npos) return false;
            const auto entry=journal.substr(offset,end-offset);
            if (!ValidKey(entry,saved_date) || entry == key) return false;
            offset=end+1;
        }
        return date > saved_date || journal.size()+key.size()+1 <= kMaxBytes;
    }
    static bool Append(std::string& journal, std::string_view date, std::string_view key) {
        if (!CanAccept(journal,date,key)) return false;
        if (journal.empty() || date > std::string_view(journal).substr(0,10)) {
            journal.assign(date);
            journal += '\n';
        }
        journal.append(key);
        journal += '\n';
        return true;
    }

private:
    static bool ValidDate(std::string_view date) {
        if (date.size()!=10) return false;
        for (size_t i=0; i<date.size(); ++i) {
            if (i==4 || i==7) { if (date[i]!='-') return false; }
            else if (date[i]<'0' || date[i]>'9') return false;
        }
        return true;
    }
    static bool ValidKey(std::string_view key, std::string_view date) {
        const auto p=key.find('|');
        if (key.size()>kMaxKey || p==0 || p==std::string_view::npos ||
            key.size()<=p+12 || key.substr(p+1,10)!=date || key[p+11]!='|' ||
            key.find_first_of("\r\n",p+12)!=std::string_view::npos ||
            key.find('|',p+12)!=std::string_view::npos) return false;
        for(char c : key.substr(0,p)) if(c<'0' || c>'9') return false;
        return true;
    }
};
}
