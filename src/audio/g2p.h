#pragma once
#include <string>
#include <vector>

namespace fr {

/// Minimal English grapheme-to-phoneme: a small exception dictionary of the most frequent words
/// plus letter-to-sound rules. Output is ARPAbet without stress (AA AE AH AO AW AY B CH D DH EH
/// ER EY F G HH IH IY JH K L M N NG OW OY P R S SH T TH UH UW V W Y Z ZH). Accuracy is what a
/// rule set can give (~75-80 % phones on running text); viseme classes are far more forgiving
/// than phones, which is all the aligner needs. Words already written as ARPAbet in brackets,
/// e.g. "[HH AH L OW]", are passed through verbatim.
std::vector<std::string> wordToPhonemes(const std::string& word);

struct TranscriptWord { std::string text; std::vector<std::string> phones; };
/// Tokenises free text into words (punctuation stripped, digits spelled out 0-9) and converts each.
std::vector<TranscriptWord> transcriptToPhonemes(const std::string& text);

} // namespace fr
