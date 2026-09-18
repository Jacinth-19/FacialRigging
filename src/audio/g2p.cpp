#include "audio/g2p.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace fr {

namespace {

// Frequent / irregular words (CMUdict-style, stress removed).
const std::map<std::string, const char*>& lexicon() {
    static const std::map<std::string, const char*> m = {
        {"the", "DH AH"}, {"a", "AH"}, {"an", "AE N"}, {"and", "AE N D"}, {"of", "AH V"}, {"to", "T UW"}, {"in", "IH N"},
        {"is", "IH Z"}, {"it", "IH T"}, {"you", "Y UW"}, {"that", "DH AE T"}, {"he", "HH IY"}, {"she", "SH IY"}, {"we", "W IY"},
        {"was", "W AA Z"}, {"for", "F AO R"}, {"on", "AA N"}, {"are", "AA R"}, {"as", "AE Z"}, {"with", "W IH DH"}, {"his", "HH IH Z"},
        {"they", "DH EY"}, {"i", "AY"}, {"at", "AE T"}, {"be", "B IY"}, {"this", "DH IH S"}, {"have", "HH AE V"}, {"from", "F R AH M"},
        {"or", "AO R"}, {"one", "W AH N"}, {"had", "HH AE D"}, {"by", "B AY"}, {"word", "W ER D"}, {"but", "B AH T"}, {"not", "N AA T"},
        {"what", "W AH T"}, {"all", "AO L"}, {"were", "W ER"}, {"when", "W EH N"}, {"your", "Y AO R"}, {"can", "K AE N"}, {"said", "S EH D"},
        {"there", "DH EH R"}, {"use", "Y UW Z"}, {"each", "IY CH"}, {"which", "W IH CH"}, {"do", "D UW"}, {"how", "HH AW"}, {"their", "DH EH R"},
        {"if", "IH F"}, {"will", "W IH L"}, {"up", "AH P"}, {"other", "AH DH ER"}, {"about", "AH B AW T"}, {"out", "AW T"}, {"many", "M EH N IY"},
        {"then", "DH EH N"}, {"them", "DH EH M"}, {"these", "DH IY Z"}, {"so", "S OW"}, {"some", "S AH M"}, {"her", "HH ER"}, {"would", "W UH D"},
        {"make", "M EY K"}, {"like", "L AY K"}, {"him", "HH IH M"}, {"into", "IH N T UW"}, {"time", "T AY M"}, {"has", "HH AE Z"}, {"look", "L UH K"},
        {"two", "T UW"}, {"more", "M AO R"}, {"write", "R AY T"}, {"go", "G OW"}, {"see", "S IY"}, {"number", "N AH M B ER"}, {"no", "N OW"},
        {"way", "W EY"}, {"could", "K UH D"}, {"people", "P IY P AH L"}, {"my", "M AY"}, {"than", "DH AE N"}, {"first", "F ER S T"},
        {"water", "W AO T ER"}, {"been", "B IH N"}, {"call", "K AO L"}, {"who", "HH UW"}, {"oil", "OY L"}, {"its", "IH T S"}, {"now", "N AW"},
        {"find", "F AY N D"}, {"long", "L AO NG"}, {"down", "D AW N"}, {"day", "D EY"}, {"did", "D IH D"}, {"get", "G EH T"}, {"come", "K AH M"},
        {"made", "M EY D"}, {"may", "M EY"}, {"part", "P AA R T"}, {"over", "OW V ER"}, {"new", "N UW"}, {"sound", "S AW N D"}, {"take", "T EY K"},
        {"only", "OW N L IY"}, {"little", "L IH T AH L"}, {"work", "W ER K"}, {"know", "N OW"}, {"place", "P L EY S"}, {"year", "Y IH R"},
        {"live", "L IH V"}, {"me", "M IY"}, {"back", "B AE K"}, {"give", "G IH V"}, {"most", "M OW S T"}, {"very", "V EH R IY"}, {"after", "AE F T ER"},
        {"thing", "TH IH NG"}, {"our", "AW ER"}, {"just", "JH AH S T"}, {"name", "N EY M"}, {"good", "G UH D"}, {"sentence", "S EH N T AH N S"},
        {"man", "M AE N"}, {"think", "TH IH NG K"}, {"say", "S EY"}, {"great", "G R EY T"}, {"where", "W EH R"}, {"help", "HH EH L P"},
        {"through", "TH R UW"}, {"much", "M AH CH"}, {"before", "B IH F AO R"}, {"line", "L AY N"}, {"right", "R AY T"}, {"too", "T UW"},
        {"mean", "M IY N"}, {"old", "OW L D"}, {"any", "EH N IY"}, {"same", "S EY M"}, {"tell", "T EH L"}, {"boy", "B OY"}, {"follow", "F AA L OW"},
        {"came", "K EY M"}, {"want", "W AA N T"}, {"show", "SH OW"}, {"also", "AO L S OW"}, {"around", "ER AW N D"}, {"form", "F AO R M"},
        {"three", "TH R IY"}, {"small", "S M AO L"}, {"set", "S EH T"}, {"put", "P UH T"}, {"end", "EH N D"}, {"does", "D AH Z"}, {"another", "AH N AH DH ER"},
        {"well", "W EH L"}, {"large", "L AA R JH"}, {"must", "M AH S T"}, {"big", "B IH G"}, {"even", "IY V AH N"}, {"such", "S AH CH"},
        {"because", "B IH K AO Z"}, {"turn", "T ER N"}, {"here", "HH IY R"}, {"why", "W AY"}, {"ask", "AE S K"}, {"went", "W EH N T"}, {"men", "M EH N"},
        {"read", "R IY D"}, {"need", "N IY D"}, {"land", "L AE N D"}, {"different", "D IH F R AH N T"}, {"home", "HH OW M"}, {"us", "AH S"},
        {"move", "M UW V"}, {"try", "T R AY"}, {"kind", "K AY N D"}, {"hand", "HH AE N D"}, {"picture", "P IH K CH ER"}, {"again", "AH G EH N"},
        {"change", "CH EY N JH"}, {"off", "AO F"}, {"play", "P L EY"}, {"spell", "S P EH L"}, {"air", "EH R"}, {"away", "AH W EY"}, {"animal", "AE N AH M AH L"},
        {"house", "HH AW S"}, {"point", "P OY N T"}, {"page", "P EY JH"}, {"letter", "L EH T ER"}, {"mother", "M AH DH ER"}, {"answer", "AE N S ER"},
        {"found", "F AW N D"}, {"study", "S T AH D IY"}, {"still", "S T IH L"}, {"learn", "L ER N"}, {"should", "SH UH D"}, {"world", "W ER L D"},
        {"high", "HH AY"}, {"every", "EH V R IY"}, {"near", "N IY R"}, {"add", "AE D"}, {"food", "F UW D"}, {"between", "B IH T W IY N"},
        {"own", "OW N"}, {"below", "B IH L OW"}, {"country", "K AH N T R IY"}, {"plant", "P L AE N T"}, {"last", "L AE S T"}, {"school", "S K UW L"},
        {"father", "F AA DH ER"}, {"keep", "K IY P"}, {"tree", "T R IY"}, {"never", "N EH V ER"}, {"start", "S T AA R T"}, {"city", "S IH T IY"},
        {"earth", "ER TH"}, {"eye", "AY"}, {"eyes", "AY Z"}, {"light", "L AY T"}, {"thought", "TH AO T"}, {"head", "HH EH D"}, {"under", "AH N D ER"},
        {"story", "S T AO R IY"}, {"saw", "S AO"}, {"left", "L EH F T"}, {"don't", "D OW N T"}, {"few", "F Y UW"}, {"while", "W AY L"}, {"along", "AH L AO NG"},
        {"might", "M AY T"}, {"close", "K L OW S"}, {"something", "S AH M TH IH NG"}, {"seem", "S IY M"}, {"next", "N EH K S T"}, {"hard", "HH AA R D"},
        {"open", "OW P AH N"}, {"example", "IH G Z AE M P AH L"}, {"begin", "B IH G IH N"}, {"life", "L AY F"}, {"always", "AO L W EY Z"}, {"those", "DH OW Z"},
        {"both", "B OW TH"}, {"paper", "P EY P ER"}, {"together", "T AH G EH DH ER"}, {"got", "G AA T"}, {"group", "G R UW P"}, {"often", "AO F AH N"},
        {"run", "R AH N"}, {"important", "IH M P AO R T AH N T"}, {"until", "AH N T IH L"}, {"children", "CH IH L D R AH N"}, {"side", "S AY D"},
        {"feet", "F IY T"}, {"car", "K AA R"}, {"mile", "M AY L"}, {"night", "N AY T"}, {"walk", "W AO K"}, {"white", "W AY T"}, {"sea", "S IY"},
        {"began", "B IH G AE N"}, {"grow", "G R OW"}, {"took", "T UH K"}, {"river", "R IH V ER"}, {"four", "F AO R"}, {"carry", "K AE R IY"},
        {"state", "S T EY T"}, {"once", "W AH N S"}, {"book", "B UH K"}, {"hear", "HH IY R"}, {"stop", "S T AA P"}, {"without", "W IH TH AW T"},
        {"second", "S EH K AH N D"}, {"later", "L EY T ER"}, {"miss", "M IH S"}, {"idea", "AY D IY AH"}, {"enough", "IH N AH F"}, {"eat", "IY T"},
        {"face", "F EY S"}, {"watch", "W AA CH"}, {"far", "F AA R"}, {"really", "R IH L IY"}, {"almost", "AO L M OW S T"}, {"let", "L EH T"},
        {"above", "AH B AH V"}, {"girl", "G ER L"}, {"sometimes", "S AH M T AY M Z"}, {"mountain", "M AW N T AH N"}, {"cut", "K AH T"}, {"young", "Y AH NG"},
        {"talk", "T AO K"}, {"soon", "S UW N"}, {"list", "L IH S T"}, {"song", "S AO NG"}, {"being", "B IY IH NG"}, {"leave", "L IY V"}, {"family", "F AE M AH L IY"},
        {"body", "B AA D IY"}, {"music", "M Y UW Z IH K"}, {"color", "K AH L ER"}, {"stand", "S T AE N D"}, {"sun", "S AH N"}, {"question", "K W EH S CH AH N"},
        {"fish", "F IH SH"}, {"area", "EH R IY AH"}, {"mark", "M AA R K"}, {"dog", "D AO G"}, {"horse", "HH AO R S"}, {"bird", "B ER D"}, {"problem", "P R AA B L AH M"},
        {"complete", "K AH M P L IY T"}, {"room", "R UW M"}, {"knew", "N UW"}, {"since", "S IH N S"}, {"ever", "EH V ER"}, {"piece", "P IY S"},
        {"told", "T OW L D"}, {"usually", "Y UW ZH AH W AH L IY"}, {"friend", "F R EH N D"}, {"easy", "IY Z IY"}, {"heard", "HH ER D"}, {"order", "AO R D ER"},
        {"red", "R EH D"}, {"door", "D AO R"}, {"sure", "SH UH R"}, {"become", "B IH K AH M"}, {"top", "T AA P"}, {"ship", "SH IH P"}, {"across", "AH K R AO S"},
        {"today", "T AH D EY"}, {"during", "D UH R IH NG"}, {"short", "SH AO R T"}, {"better", "B EH T ER"}, {"best", "B EH S T"}, {"however", "HH AW EH V ER"},
        {"low", "L OW"}, {"hours", "AW ER Z"}, {"black", "B L AE K"}, {"products", "P R AA D AH K T S"}, {"happened", "HH AE P AH N D"}, {"whole", "HH OW L"},
        {"measure", "M EH ZH ER"}, {"remember", "R IH M EH M B ER"}, {"early", "ER L IY"}, {"waves", "W EY V Z"}, {"reached", "R IY CH T"}, {"listen", "L IH S AH N"},
        {"wind", "W IH N D"}, {"rock", "R AA K"}, {"space", "S P EY S"}, {"covered", "K AH V ER D"}, {"fast", "F AE S T"}, {"several", "S EH V R AH L"},
        {"hold", "HH OW L D"}, {"himself", "HH IH M S EH L F"}, {"toward", "T AH W AO R D"}, {"five", "F AY V"}, {"step", "S T EH P"}, {"morning", "M AO R N IH NG"},
        {"passed", "P AE S T"}, {"vowel", "V AW AH L"}, {"true", "T R UW"}, {"hundred", "HH AH N D R AH D"}, {"against", "AH G EH N S T"}, {"pattern", "P AE T ER N"},
        {"numeral", "N UW M ER AH L"}, {"table", "T EY B AH L"}, {"north", "N AO R TH"}, {"slowly", "S L OW L IY"}, {"money", "M AH N IY"}, {"map", "M AE P"},
        {"farm", "F AA R M"}, {"pulled", "P UH L D"}, {"draw", "D R AO"}, {"voice", "V OY S"}, {"seen", "S IY N"}, {"cold", "K OW L D"}, {"cried", "K R AY D"},
        {"plan", "P L AE N"}, {"notice", "N OW T AH S"}, {"south", "S AW TH"}, {"sing", "S IH NG"}, {"war", "W AO R"}, {"ground", "G R AW N D"}, {"fall", "F AO L"},
        {"king", "K IH NG"}, {"town", "T AW N"}, {"i'll", "AY L"}, {"unit", "Y UW N IH T"}, {"figure", "F IH G Y ER"}, {"certain", "S ER T AH N"}, {"field", "F IY L D"},
        {"travel", "T R AE V AH L"}, {"wood", "W UH D"}, {"fire", "F AY ER"}, {"upon", "AH P AA N"}, {"done", "D AH N"}, {"english", "IH NG G L IH SH"},
        {"road", "R OW D"}, {"half", "HH AE F"}, {"ten", "T EH N"}, {"fly", "F L AY"}, {"gave", "G EY V"}, {"box", "B AA K S"}, {"finally", "F AY N AH L IY"},
        {"wait", "W EY T"}, {"correct", "K ER EH K T"}, {"oh", "OW"}, {"quickly", "K W IH K L IY"}, {"person", "P ER S AH N"}, {"became", "B IH K EY M"},
        {"shown", "SH OW N"}, {"minutes", "M IH N AH T S"}, {"strong", "S T R AO NG"}, {"verb", "V ER B"}, {"stars", "S T AA R Z"}, {"front", "F R AH N T"},
        {"feel", "F IY L"}, {"fact", "F AE K T"}, {"inches", "IH N CH AH Z"}, {"street", "S T R IY T"}, {"decided", "D IH S AY D AH D"}, {"contain", "K AH N T EY N"},
        {"course", "K AO R S"}, {"surface", "S ER F AH S"}, {"produce", "P R AH D UW S"}, {"building", "B IH L D IH NG"}, {"ocean", "OW SH AH N"}, {"class", "K L AE S"},
        {"note", "N OW T"}, {"nothing", "N AH TH IH NG"}, {"rest", "R EH S T"}, {"carefully", "K EH R F AH L IY"}, {"scientists", "S AY AH N T IH S T S"},
        {"inside", "IH N S AY D"}, {"wheels", "W IY L Z"}, {"stay", "S T EY"}, {"green", "G R IY N"}, {"known", "N OW N"}, {"island", "AY L AH N D"},
        {"week", "W IY K"}, {"less", "L EH S"}, {"machine", "M AH SH IY N"}, {"base", "B EY S"}, {"ago", "AH G OW"}, {"stood", "S T UH D"}, {"plane", "P L EY N"},
        {"system", "S IH S T AH M"}, {"behind", "B IH HH AY N D"}, {"ran", "R AE N"}, {"round", "R AW N D"}, {"boat", "B OW T"}, {"game", "G EY M"}, {"force", "F AO R S"},
        {"brought", "B R AO T"}, {"understand", "AH N D ER S T AE N D"}, {"warm", "W AO R M"}, {"common", "K AA M AH N"}, {"bring", "B R IH NG"}, {"explain", "IH K S P L EY N"},
        {"dry", "D R AY"}, {"though", "DH OW"}, {"language", "L AE NG G W AH JH"}, {"shape", "SH EY P"}, {"deep", "D IY P"}, {"thousands", "TH AW Z AH N D Z"},
        {"yes", "Y EH S"}, {"clear", "K L IY R"}, {"equation", "IH K W EY ZH AH N"}, {"yet", "Y EH T"}, {"government", "G AH V ER M AH N T"}, {"filled", "F IH L D"},
        {"heat", "HH IY T"}, {"full", "F UH L"}, {"hot", "HH AA T"}, {"check", "CH EH K"}, {"object", "AA B JH EH K T"}, {"am", "AE M"}, {"rule", "R UW L"},
        {"among", "AH M AH NG"}, {"noun", "N AW N"}, {"power", "P AW ER"}, {"cannot", "K AE N AA T"}, {"able", "EY B AH L"}, {"six", "S IH K S"}, {"size", "S AY Z"},
        {"dark", "D AA R K"}, {"ball", "B AO L"}, {"material", "M AH T IH R IY AH L"}, {"special", "S P EH SH AH L"}, {"heavy", "HH EH V IY"}, {"fine", "F AY N"},
        {"pair", "P EH R"}, {"circle", "S ER K AH L"}, {"include", "IH N K L UW D"}, {"built", "B IH L T"}, {"hello", "HH AH L OW"}, {"hi", "HH AY"}, {"please", "P L IY Z"},
        {"thank", "TH AE NG K"}, {"thanks", "TH AE NG K S"}, {"welcome", "W EH L K AH M"}, {"facial", "F EY SH AH L"}, {"rigging", "R IH G IH NG"}, {"animation", "AE N AH M EY SH AH N"},
        {"mouth", "M AW TH"}, {"speech", "S P IY CH"}, {"audio", "AA D IY OW"}, {"model", "M AA D AH L"}, {"quick", "K W IH K"}, {"brown", "B R AW N"}, {"fox", "F AA K S"},
        {"jumps", "JH AH M P S"}, {"lazy", "L EY Z IY"}, {"she's", "SH IY Z"}, {"it's", "IH T S"}, {"i'm", "AY M"}, {"you're", "Y UH R"}, {"we're", "W IH R"},
        {"can't", "K AE N T"}, {"won't", "W OW N T"}, {"isn't", "IH Z AH N T"}, {"there's", "DH EH R Z"}, {"that's", "DH AE T S"}, {"what's", "W AH T S"},
        {"zero", "Z IH R OW"}, {"seven", "S EH V AH N"}, {"eight", "EY T"}, {"nine", "N AY N"}, {"had", "HH AE D"}, {"dark", "D AA R K"}, {"suit", "S UW T"},
        {"greasy", "G R IY S IY"}, {"wash", "W AA SH"}, {"year", "Y IH R"}, {"carry", "K AE R IY"}, {"oily", "OY L IY"}, {"rag", "R AE G"}, {"like", "L AY K"},
    };
    return m;
}

bool isVowel(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }

// Letter-to-sound rules for the residue. Context: the whole word `w`, index `i`. Returns phones
// and how many letters were consumed.
struct RuleHit { const char* phones; int len; };
RuleHit rule(const std::string& w, size_t i) {
    auto at = [&](size_t k) -> char { return k < w.size() ? w[k] : '\0'; };
    auto starts = [&](const char* s) { size_t n = std::char_traits<char>::length(s); return w.compare(i, n, s) == 0; };
    const char c = w[i], n = at(i + 1), nn = at(i + 2);
    const bool last = i + 1 == w.size();
    const bool magicE = (n != '\0' && !isVowel(n) && n != 'y' && at(i + 2) == 'e' && i + 3 == w.size()) // a_e
                     || (n != '\0' && !isVowel(n) && at(i + 2) == 'e' && at(i + 3) == 's' && i + 4 == w.size()) || (n != '\0' && !isVowel(n) && at(i + 2) == 'e' && at(i + 3) == 'd' && i + 4 == w.size());
    switch (c) {
    case 'a':
        if (starts("ai") || starts("ay")) return {"EY", 2};
        if (starts("au") || starts("aw")) return {"AO", 2};
        if (starts("ar")) return {"AA R", 2};
        if (starts("al") && (nn == 'l' || nn == 'k')) return {"AO", 1};
        if (magicE) return {"EY", 1};
        if (last) return {"AH", 1};
        return {"AE", 1};
    case 'e':
        if (starts("ee") || starts("ea")) return {"IY", 2};
        if (starts("ei") || starts("ey")) return {"EY", 2};
        if (starts("ew")) return {"UW", 2};
        if (starts("er") && (i + 2 == w.size() || !isVowel(nn))) return {"ER", 2};
        if (last) return {"", 1}; // silent final e
        if (last == false && i + 2 == w.size() && n == 'd') return {"", 1}; // -ed (approximation)
        return {"EH", 1};
    case 'i':
        if (starts("igh")) return {"AY", 3};
        if (starts("ir")) return {"ER", 2};
        if (starts("ie") && i + 2 == w.size()) return {"AY", 2};
        if (starts("ie")) return {"IY", 2};
        if (magicE || (last && i > 0)) return {"AY", 1};
        return {"IH", 1};
    case 'o':
        if (starts("oo")) return {(nn == 'k' || nn == 'd') ? "UH" : "UW", 2};
        if (starts("ou") || starts("ow")) return {(i + 2 == w.size() && c == 'o' && n == 'w') ? "OW" : "AW", 2};
        if (starts("oi") || starts("oy")) return {"OY", 2};
        if (starts("or")) return {"AO R", 2};
        if (starts("oa")) return {"OW", 2};
        if (magicE || last) return {"OW", 1};
        return {"AA", 1};
    case 'u':
        if (starts("ur")) return {"ER", 2};
        if (magicE) return {"Y UW", 1};
        if (last) return {"UW", 1};
        return {"AH", 1};
    case 'y':
        if (i == 0) return {"Y", 1};
        if (last) return {(i >= 3) ? "IY" : "AY", 1};
        return {"IH", 1};
    case 'b': return {(starts("bb")) ? "B" : "B", starts("bb") ? 2 : 1};
    case 'c':
        if (starts("ch")) return {"CH", 2};
        if (starts("ck")) return {"K", 2};
        if (n == 'e' || n == 'i' || n == 'y') return {"S", 1};
        return {"K", 1};
    case 'd': return {starts("dd") ? "D" : (starts("dg") ? "JH" : "D"), starts("dd") || starts("dg") ? 2 : 1};
    case 'f': return {"F", starts("ff") ? 2 : 1};
    case 'g':
        if (starts("gh")) return {(i == 0) ? "G" : "", 2};
        if (starts("gg")) return {"G", 2};
        if (n == 'e' || n == 'i' || n == 'y') return {"JH", 1};
        return {"G", 1};
    case 'h': return {"HH", 1};
    case 'j': return {"JH", 1};
    case 'k':
        if (i == 0 && n == 'n') return {"", 1};
        return {"K", 1};
    case 'l': return {"L", starts("ll") ? 2 : 1};
    case 'm': return {"M", starts("mm") ? 2 : 1};
    case 'n':
        if (starts("ng") && i + 2 == w.size()) return {"NG", 2};
        if (starts("nk")) return {"NG K", 2};
        return {"N", starts("nn") ? 2 : 1};
    case 'p':
        if (starts("ph")) return {"F", 2};
        return {"P", starts("pp") ? 2 : 1};
    case 'q': return {"K W", (n == 'u') ? 2 : 1};
    case 'r': return {"R", starts("rr") ? 2 : 1};
    case 's':
        if (starts("sh")) return {"SH", 2};
        if (starts("ss")) return {"S", 2};
        if (starts("sion")) return {"ZH AH N", 4};
        if (last && i > 0 && (isVowel(w[i - 1]) || w[i - 1] == 'n' || w[i - 1] == 'r' || w[i - 1] == 'l' || w[i - 1] == 'd' || w[i - 1] == 'g' || w[i - 1] == 'b' || w[i - 1] == 'm')) return {"Z", 1};
        return {"S", 1};
    case 't':
        if (starts("th")) return {(i == 0 && (starts("the") || starts("thi") || starts("tha") || starts("tho"))) ? "DH" : "TH", 2};
        if (starts("tion")) return {"SH AH N", 4};
        if (starts("tch")) return {"CH", 3};
        return {"T", starts("tt") ? 2 : 1};
    case 'v': return {"V", 1};
    case 'w':
        if (starts("wh")) return {"W", 2};
        if (i == 0 && n == 'r') return {"", 1};
        return {"W", 1};
    case 'x': return {"K S", 1};
    case 'z': return {"Z", starts("zz") ? 2 : 1};
    default: return {"", 1};
    }
}

std::vector<std::string> split(const std::string& s) { std::vector<std::string> v; std::istringstream is(s); std::string t; while (is >> t) v.push_back(t); return v; }

} // namespace

std::vector<std::string> wordToPhonemes(const std::string& wordIn) {
    if (wordIn.size() > 2 && wordIn.front() == '[' && wordIn.back() == ']') {
        auto v = split(wordIn.substr(1, wordIn.size() - 2)); for (auto& p : v) for (auto& c : p) c = char(std::toupper((unsigned char)c)); return v;
    }
    std::string w; for (char c : wordIn) { if (std::isalpha((unsigned char)c) || c == '\'') w += char(std::tolower((unsigned char)c)); }
    if (w.empty()) return {};
    auto it = lexicon().find(w);
    if (it != lexicon().end()) return split(it->second);
    // plural / past / -ing of a lexicon word
    if (w.size() > 3) {
        if (w.back() == 's' && lexicon().count(w.substr(0, w.size() - 1))) { auto v = split(lexicon().at(w.substr(0, w.size() - 1))); v.push_back((v.back() == "T" || v.back() == "K" || v.back() == "P" || v.back() == "F") ? "S" : "Z"); return v; }
        if (w.compare(w.size() - 3, 3, "ing") == 0 && lexicon().count(w.substr(0, w.size() - 3))) { auto v = split(lexicon().at(w.substr(0, w.size() - 3))); v.push_back("IH"); v.push_back("NG"); return v; }
        if (w.compare(w.size() - 2, 2, "ed") == 0 && lexicon().count(w.substr(0, w.size() - 2))) { auto v = split(lexicon().at(w.substr(0, w.size() - 2))); v.push_back((v.back() == "T" || v.back() == "D") ? "AH D" : (v.back() == "K" || v.back() == "P" || v.back() == "S" || v.back() == "F" || v.back() == "SH" || v.back() == "CH") ? "T" : "D"); return v; }
    }
    std::vector<std::string> out;
    std::string letters; for (char c : w) if (c != '\'') letters += c;
    for (size_t i = 0; i < letters.size();) {
        RuleHit h = rule(letters, i);
        for (const auto& p : split(h.phones)) out.push_back(p);
        i += size_t(std::max(1, h.len));
    }
    // Words with no vowel phone (e.g. "hmm"): give them a schwa so the mouth does something.
    bool hasV = false; for (auto& p : out) if (p == "AA" || p == "AE" || p == "AH" || p == "AO" || p == "AW" || p == "AY" || p == "EH" || p == "ER" || p == "EY" || p == "IH" || p == "IY" || p == "OW" || p == "OY" || p == "UH" || p == "UW") hasV = true;
    if (!hasV && !out.empty()) out.insert(out.begin() + long(out.size() / 2), "AH");
    return out;
}

std::vector<TranscriptWord> transcriptToPhonemes(const std::string& text) {
    static const char* digits[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
    std::vector<TranscriptWord> words;
    std::string cur; bool inBracket = false;
    auto flush = [&]() { if (!cur.empty()) { TranscriptWord tw; tw.text = cur; tw.phones = wordToPhonemes(cur); if (!tw.phones.empty()) words.push_back(tw); cur.clear(); } };
    for (char c : text) {
        if (c == '[') { flush(); inBracket = true; cur += c; continue; }
        if (c == ']') { cur += c; inBracket = false; flush(); continue; }
        if (inBracket) { cur += c; continue; }
        if (std::isdigit((unsigned char)c)) { flush(); cur = digits[c - '0']; flush(); continue; }
        if (std::isalpha((unsigned char)c) || c == '\'') cur += c; else flush();
    }
    flush();
    return words;
}

} // namespace fr
