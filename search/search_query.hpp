#pragma once
#include "intermediate_result.hpp"

#include "../geometry/rect2d.hpp"

#include "../base/buffer_vector.hpp"
#include "../base/limited_priority_queue.hpp"
#include "../base/string_utils.hpp"

#include "../std/function.hpp"
#include "../std/map.hpp"
#include "../std/scoped_ptr.hpp"
#include "../std/shared_ptr.hpp"
#include "../std/string.hpp"
#include "../std/unordered_set.hpp"
#include "../std/vector.hpp"


class FeatureType;
class Index;
class MwmInfo;

namespace storage { class CountryInfoGetter; }

namespace search
{

struct CategoryInfo;
class LangKeywordsScorer;
namespace impl { class IntermediateResult; struct FeatureLoader; class BestNameFinder; }

class Query
{
public:
  // Map category_token -> category_type.
  typedef multimap<strings::UniString, uint32_t> CategoriesMapT;
  // Vector of pairs (string_to_suggest, min_prefix_length_to_suggest).
  typedef vector<pair<strings::UniString, uint8_t> > StringsToSuggestVectorT;

  Query(Index const * pIndex,
        CategoriesMapT const * pCategories,
        StringsToSuggestVectorT const * pStringsToSuggest,
        storage::CountryInfoGetter const * pInfoGetter);
  ~Query();

  void SetViewport(m2::RectD const & viewport);
  void SetPreferredLanguage(string const & lang);
  void Search(string const & query,
              function<void (Result const &)> const & f,
              unsigned int resultsNeeded = 10);

  void ClearCache();

private:

  friend struct impl::FeatureLoader;
  friend class impl::BestNameFinder;

  typedef impl::IntermediateResult ResultT;
  typedef shared_ptr<ResultT> ValueT;

  void AddResult(ValueT const & result);
  void AddFeatureResult(FeatureType const & f, string const & fName);
  void FlushResults(function<void (Result const &)> const & f);
  void UpdateViewportOffsets();
  void SearchFeatures();
  void SearchFeatures(vector<vector<strings::UniString> > const & tokens,
                      vector<MwmInfo> const & mwmInfo,
                      unordered_set<int8_t> const & langs,
                      bool onlyInViewport);

  void SuggestStrings();
  void MatchForSuggestions(strings::UniString const & token);

  void GetBestMatchName(FeatureType const & f, uint32_t & penalty, string & name);

  Index const * m_pIndex;
  CategoriesMapT const * m_pCategories;
  StringsToSuggestVectorT const * m_pStringsToSuggest;
  storage::CountryInfoGetter const * m_pInfoGetter;
  int m_preferredLanguage;

  string m_rawQuery;
  strings::UniString m_uniQuery;
  buffer_vector<strings::UniString, 32> m_tokens;
  strings::UniString m_prefix;
  m2::RectD m_viewport;
  m2::RectD m_viewportExtended;

  scoped_ptr<LangKeywordsScorer> m_pKeywordsScorer;

  bool m_bOffsetsCacheIsValid;
  vector<unordered_set<uint32_t> > m_offsetsInViewport;

  class CompareT
  {
    typedef bool (*FunctionT) (ResultT const &, ResultT const &);
    FunctionT m_fn;

  public:
    CompareT() : m_fn(0) {}
    explicit CompareT(FunctionT const & fn) : m_fn(fn) {}

    inline bool operator() (ValueT const & v1, ValueT const & v2) const
    {
      return m_fn(*v1, *v2);
    }
  };

  typedef my::limited_priority_queue<ValueT, CompareT> QueueT;
  static const size_t m_qCount = 2;
  QueueT m_results[m_qCount];
};

}  // namespace search
