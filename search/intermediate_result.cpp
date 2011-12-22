#include "intermediate_result.hpp"

#include "../storage/country_info.hpp"

#include "../indexer/feature_utils.hpp"
#include "../indexer/mercator.hpp"

#include "../geometry/angles.hpp"
#include "../geometry/distance_on_sphere.hpp"

#include "../base/string_utils.hpp"
#include "../base/logging.hpp"


namespace search
{
namespace impl
{

IntermediateResult::IntermediateResult(m2::RectD const & viewportRect,
                                       FeatureType const & f,
                                       string const & displayName,
                                       string const & fileName)
  : m_str(displayName),
    m_rect(feature::GetFeatureViewport(f)),
    m_resultType(RESULT_FEATURE)
{
  // get feature type
  FeatureType::GetTypesFn types;
  f.ForEachTypeRef(types);
  ASSERT_GREATER(types.m_size, 0, ());
  m_type = types.m_types[0];

  // get region info
  if (!fileName.empty())
    m_region.SetName(fileName);
  else
  {
    if (f.GetFeatureType() == feature::GEOM_POINT)
      m_region.SetPoint(f.GetCenter());
  }

  // get common params
  m_distance = ResultDistance(viewportRect.Center(), m_rect.Center());
  m_direction = ResultDirection(viewportRect.Center(), m_rect.Center());
  m_searchRank = feature::GetSearchRank(f);
  m_viewportDistance = ViewportDistance(viewportRect, m_rect.Center());
}

IntermediateResult::IntermediateResult(m2::RectD const & viewportRect,
                                       double lat, double lon, double precision)
  : m_str("(" + strings::to_string(lat) + ", " + strings::to_string(lon) + ")"),
    m_rect(MercatorBounds::LonToX(lon - precision), MercatorBounds::LatToY(lat - precision),
           MercatorBounds::LonToX(lon + precision), MercatorBounds::LatToY(lat + precision)),
    m_type(0), m_resultType(RESULT_LATLON), m_searchRank(0)
{
  // get common params
  m_distance = ResultDistance(viewportRect.Center(), m_rect.Center());
  m_direction = ResultDirection(viewportRect.Center(), m_rect.Center());
  m_viewportDistance = ViewportDistance(viewportRect, m_rect.Center());

  // get region info
  m_region.SetPoint(m2::PointD(MercatorBounds::LonToX(lon),
                               MercatorBounds::LatToY(lat)));
}

IntermediateResult::IntermediateResult(string const & name, int penalty)
  : m_str(name), m_completionString(name + " "),
    /// @todo ??? Maybe we should initialize here by maximum value ???
    m_distance(0), m_direction(0), m_viewportDistance(0),
    m_resultType(RESULT_CATEGORY),
    m_searchRank(0)
{
}

/*
bool IntermediateResult::LessOrderF::operator()
          (IntermediateResult const & r1, IntermediateResult const & r2) const
{
  if (r1.m_resultType != r2.m_resultType)
    return (r1.m_resultType < r2.m_resultType);

  if (r1.m_searchRank != r2.m_searchRank)
    return (r1.m_searchRank > r2.m_searchRank);

  return (r1.m_distance < r2.m_distance);
}
*/

bool IntermediateResult::LessRank(IntermediateResult const & r1, IntermediateResult const & r2)
{
  return (r1.m_searchRank > r2.m_searchRank);
}

bool IntermediateResult::LessDistance(IntermediateResult const & r1, IntermediateResult const & r2)
{
  if (r1.m_distance != r2.m_distance)
    return (r1.m_distance < r2.m_distance);
  else
    return LessRank(r1, r2);
}

bool IntermediateResult::LessViewportDistance(IntermediateResult const & r1, IntermediateResult const & r2)
{
  if (r1.m_viewportDistance != r2.m_viewportDistance)
    return (r1.m_viewportDistance < r2.m_viewportDistance);
  else
    return LessRank(r1, r2);
}

Result IntermediateResult::GenerateFinalResult(storage::CountryInfoGetter const * pInfo) const
{
  switch (m_resultType)
  {
  case RESULT_FEATURE:
    return Result(m_str
              #ifdef DEBUG
                  + ' ' + strings::to_string(static_cast<int>(m_searchRank))
              #endif
                  , m_region.GetRegion(pInfo), m_type, m_rect, m_distance, m_direction);

  case RESULT_LATLON:
    return Result(m_str, m_region.GetRegion(pInfo), 0, m_rect, m_distance, m_direction);

  default:
    ASSERT_EQUAL ( m_resultType, RESULT_CATEGORY, () );
    return Result(m_str, m_completionString);
  }
}

double IntermediateResult::ResultDistance(m2::PointD const & a, m2::PointD const & b)
{
  return ms::DistanceOnEarth(MercatorBounds::YToLat(a.y), MercatorBounds::XToLon(a.x),
                             MercatorBounds::YToLat(b.y), MercatorBounds::XToLon(b.x));
}

double IntermediateResult::ResultDirection(m2::PointD const & a, m2::PointD const & b)
{
  return ang::AngleTo(a, b);
}

int IntermediateResult::ViewportDistance(m2::RectD const & viewport, m2::PointD const & p)
{
  if (viewport.IsPointInside(p))
    return 0;

  m2::RectD r = viewport;
  r.Scale(3);
  if (r.IsPointInside(p))
    return 1;

  r = viewport;
  r.Scale(5);
  if (r.IsPointInside(p))
    return 2;

  return 3;
}

bool IntermediateResult::StrictEqualF::operator()(IntermediateResult const & r) const
{
  if (m_r.m_resultType == r.m_resultType && m_r.m_resultType == RESULT_FEATURE)
  {
    if (m_r.m_str == r.m_str && m_r.m_type == r.m_type)
    {
      /// @todo Tune this constant.
      return fabs(m_r.m_distance - r.m_distance) < 500.0;
    }
  }

  return false;
}

namespace
{
  uint8_t FirstLevelIndex(uint32_t t)
  {
    uint8_t v;
    VERIFY ( ftype::GetValue(t, 0, v), (t) );
    return v;
  }

  class IsLinearChecker
  {
    static size_t const m_count = 1;
    uint8_t m_index[m_count];

  public:
    IsLinearChecker()
    {
      char const * arr[m_count] = { "highway" };

      ClassifObject const * c = classif().GetRoot();
      for (size_t i = 0; i < m_count; ++i)
        m_index[i] = static_cast<uint8_t>(c->BinaryFind(arr[i]).GetIndex());
    }

    bool IsMy(uint8_t ind) const
    {
      for (size_t i = 0; i < m_count; ++i)
        if (ind == m_index[i])
          return true;

      return false;
    }
  };
}

bool IntermediateResult::LessLinearTypesF::operator()
          (IntermediateResult const & r1, IntermediateResult const & r2) const
{
  if (r1.m_resultType != r2.m_resultType)
    return (r1.m_resultType < r2.m_resultType);

  if (r1.m_str != r2.m_str)
    return (r1.m_str < r2.m_str);

  uint8_t const i1 = FirstLevelIndex(r1.m_type);
  uint8_t const i2 = FirstLevelIndex(r2.m_type);

  if (i1 != i2)
    return (i1 < i2);

  // Should stay the best feature, after unique, so add this criteria:

  if (r1.m_searchRank != r2.m_searchRank)
    return (r1.m_searchRank > r2.m_searchRank);
  return (r1.m_distance < r2.m_distance);
}

bool IntermediateResult::EqualLinearTypesF::operator()
          (IntermediateResult const & r1, IntermediateResult const & r2) const
{
  if (r1.m_resultType == r2.m_resultType && r1.m_str == r2.m_str)
  {
    // filter equal linear features
    static IsLinearChecker checker;

    uint8_t const ind = FirstLevelIndex(r1.m_type);
    return (ind == FirstLevelIndex(r2.m_type) && checker.IsMy(ind));
  }

  return false;
}

string IntermediateResult::DebugPrint() const
{
  string res("IntermediateResult: ");
  res += "Name: " + m_str;
  res += "; Type: " + ::DebugPrint(m_type);
  res += "; Rank: " + ::DebugPrint(m_searchRank);
  res += "; Distance: " + ::DebugPrint(m_viewportDistance);
  return res;
}

string IntermediateResult::RegionInfo::GetRegion(storage::CountryInfoGetter const * pInfo) const
{
  if (!m_file.empty())
    return pInfo->GetRegionName(m_file);
  else if (m_valid)
    return pInfo->GetRegionName(m_point);
  else
    return string();
}

}  // namespace search::impl
}  // namespace search
