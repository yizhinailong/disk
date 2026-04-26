#include <cctype>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::file {
    namespace {

        struct SearchSqlPair {
            std::string count_sql;
            std::string data_sql;
        };

        enum class SearchBranch {
            All,
            File,
            Folder,
        };

        [[nodiscard]] auto NormalizeFulltextKeyword(std::string_view keyword) -> std::string {
            std::string normalized;
            normalized.reserve(keyword.size());

            bool previous_is_space = true;
            for (const auto ch : keyword) {
                if (ch == ' ') {
                    if (!previous_is_space) {
                        normalized.push_back(' ');
                    }
                    previous_is_space = true;
                    continue;
                }

                normalized.push_back(ch);
                previous_is_space = false;
            }

            if (!normalized.empty() && normalized.back() == ' ') {
                normalized.pop_back();
            }

            return normalized;
        }

        [[nodiscard]] auto IsFulltextEligible(std::string_view keyword) -> bool {
            const auto normalized = NormalizeFulltextKeyword(keyword);
            if (normalized.size() < 3) {
                return false;
            }

            for (const auto ch : normalized) {
                const auto uch = static_cast<unsigned char>(ch);
                if (uch > 0x7F || (!std::isalnum(uch) && ch != ' ')) {
                    return false;
                }
            }

            return normalized.find_first_not_of(' ') != std::string::npos;
        }

        [[nodiscard]] auto BuildSearchSql(SearchBranch branch, bool use_fulltext) -> SearchSqlPair {
            switch (branch) {
                case SearchBranch::All:
                    return use_fulltext ? SearchSqlPair{
                        .count_sql = "SELECT COUNT(*) FROM (" "  SELECT f.id, f.name, 'file' AS type " "  FROM files f " "  WHERE f.user_id = ? AND MATCH(f.name) AGAINST(? IN BOOLEAN MODE) " "  AND (? IS NULL OR f.folder_id = ?) " "  UNION ALL " "  SELECT fo.id, fo.name, 'folder' AS type " "  FROM folders fo " "  WHERE fo.user_id = ? AND MATCH(fo.name) AGAINST(? IN BOOLEAN MODE) " "  AND (? IS NULL OR fo.parent_id = ?) " ") AS combined",
                        .data_sql = "SELECT * FROM (" "  SELECT f.id, f.name, 'file' AS type, f.size, f.mime_type, " "         COALESCE(fc.hash_md5, '') AS hash, 0 AS item_count, f.path, f.created_at, f.updated_at " "  FROM files f " "  LEFT JOIN file_contents fc ON f.content_id = fc.id " "  WHERE f.user_id = ? AND MATCH(f.name) AGAINST(? IN BOOLEAN MODE) " "  AND (? IS NULL OR f.folder_id = ?) " "  UNION ALL " "  SELECT fo.id, fo.name, 'folder' AS type, 0 AS size, '' AS mime_type, " "         '' AS hash, fo.item_count, fo.path, fo.created_at, fo.updated_at " "  FROM folders fo " "  WHERE fo.user_id = ? AND MATCH(fo.name) AGAINST(? IN BOOLEAN MODE) " "  AND (? IS NULL OR fo.parent_id = ?) " ") AS combined " "ORDER BY name ASC " "LIMIT ? OFFSET ?",
                    } :
                                          SearchSqlPair{
                                              .count_sql = "SELECT COUNT(*) FROM (" "  SELECT f.id, f.name, 'file' AS type " "  FROM files f " "  WHERE f.user_id = ? AND f.name LIKE ? " "  AND (? IS NULL OR f.folder_id = ?) " "  UNION ALL " "  SELECT fo.id, fo.name, 'folder' AS type " "  FROM folders fo " "  WHERE fo.user_id = ? AND fo.name LIKE ? " "  AND (? IS NULL OR fo.parent_id = ?) " ") AS combined",
                                              .data_sql = "SELECT * FROM (" "  SELECT f.id, f.name, 'file' AS type, f.size, f.mime_type, " "         COALESCE(fc.hash_md5, '') AS hash, 0 AS item_count, f.path, f.created_at, f.updated_at " "  FROM files f " "  LEFT JOIN file_contents fc ON f.content_id = fc.id " "  WHERE f.user_id = ? AND f.name LIKE ? " "  AND (? IS NULL OR f.folder_id = ?) " "  UNION ALL " "  SELECT fo.id, fo.name, 'folder' AS type, 0 AS size, '' AS mime_type, " "         '' AS hash, fo.item_count, fo.path, fo.created_at, fo.updated_at " "  FROM folders fo " "  WHERE fo.user_id = ? AND fo.name LIKE ? " "  AND (? IS NULL OR fo.parent_id = ?) " ") AS combined " "ORDER BY name ASC " "LIMIT ? OFFSET ?",
                                          };
                case SearchBranch::File:
                    return use_fulltext ? SearchSqlPair{
                        .count_sql = "SELECT COUNT(*) FROM files f " "WHERE f.user_id = ? AND MATCH(f.name) AGAINST(? IN BOOLEAN MODE) " "AND (? IS NULL OR f.folder_id = ?)",
                        .data_sql = "SELECT f.id, f.name, f.size, f.mime_type, f.path, f.created_at, f.updated_at, " "       COALESCE(fc.hash_md5, '') AS hash " "FROM files f " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "WHERE f.user_id = ? AND MATCH(f.name) AGAINST(? IN BOOLEAN MODE) " "AND (? IS NULL OR f.folder_id = ?) " "ORDER BY name ASC " "LIMIT ? OFFSET ?",
                    } :
                                          SearchSqlPair{
                                              .count_sql = "SELECT COUNT(*) FROM files f " "WHERE f.user_id = ? AND f.name LIKE ? " "AND (? IS NULL OR f.folder_id = ?)",
                                              .data_sql = "SELECT f.id, f.name, f.size, f.mime_type, f.path, f.created_at, f.updated_at, " "       COALESCE(fc.hash_md5, '') AS hash " "FROM files f " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "WHERE f.user_id = ? AND f.name LIKE ? " "AND (? IS NULL OR f.folder_id = ?) " "ORDER BY name ASC " "LIMIT ? OFFSET ?",
                                          };
                case SearchBranch::Folder:
                    return use_fulltext ? SearchSqlPair{
                        .count_sql = "SELECT COUNT(*) FROM folders fo " "WHERE fo.user_id = ? AND MATCH(fo.name) AGAINST(? IN BOOLEAN MODE) " "AND (? IS NULL OR fo.parent_id = ?)",
                        .data_sql = "SELECT fo.id, fo.name, fo.item_count, fo.path, fo.created_at, fo.updated_at " "FROM folders fo " "WHERE fo.user_id = ? AND MATCH(fo.name) AGAINST(? IN BOOLEAN MODE) " "AND (? IS NULL OR fo.parent_id = ?) " "ORDER BY name ASC " "LIMIT ? OFFSET ?",
                    } :
                                          SearchSqlPair{
                                              .count_sql = "SELECT COUNT(*) FROM folders fo " "WHERE fo.user_id = ? AND fo.name LIKE ? " "AND (? IS NULL OR fo.parent_id = ?)",
                                              .data_sql = "SELECT fo.id, fo.name, fo.item_count, fo.path, fo.created_at, fo.updated_at " "FROM folders fo " "WHERE fo.user_id = ? AND fo.name LIKE ? " "AND (? IS NULL OR fo.parent_id = ?) " "ORDER BY name ASC " "LIMIT ? OFFSET ?",
                                          };
            }

            return {};
        }

        TEST(FileServiceSearchTest, IsFulltextEligibleAcceptsAsciiTokens) {
            EXPECT_TRUE(IsFulltextEligible("hello"));
            EXPECT_TRUE(IsFulltextEligible("test123"));
            EXPECT_TRUE(IsFulltextEligible("my file"));
            EXPECT_TRUE(IsFulltextEligible("  my   file  "));
        }

        TEST(FileServiceSearchTest, IsFulltextEligibleRejectsShortKeywords) {
            EXPECT_FALSE(IsFulltextEligible("ab"));
            EXPECT_FALSE(IsFulltextEligible("a"));
            EXPECT_FALSE(IsFulltextEligible(""));
        }

        TEST(FileServiceSearchTest, NonAsciiKeywordFallsBackToLike) {
            EXPECT_FALSE(IsFulltextEligible("测试"));
            EXPECT_FALSE(IsFulltextEligible("файл"));
            EXPECT_FALSE(IsFulltextEligible("hello-world"));
        }

        TEST(FileServiceSearchTest, ShortKeywordFallsBackToLike) {
            EXPECT_FALSE(IsFulltextEligible("ab"));
        }

        TEST(FileServiceSearchTest, FulltextSqlUsesMatchAgainst) {
            for (const auto branch : { SearchBranch::All, SearchBranch::File, SearchBranch::Folder }) {
                const auto sql = BuildSearchSql(branch, true);
                EXPECT_NE(sql.count_sql.find("MATCH"), std::string::npos);
                EXPECT_NE(sql.count_sql.find("AGAINST"), std::string::npos);
                EXPECT_NE(sql.data_sql.find("MATCH"), std::string::npos);
                EXPECT_NE(sql.data_sql.find("AGAINST"), std::string::npos);
            }
        }

        TEST(FileServiceSearchTest, LikeSqlUsesLikePattern) {
            for (const auto branch : { SearchBranch::All, SearchBranch::File, SearchBranch::Folder }) {
                const auto sql = BuildSearchSql(branch, false);
                EXPECT_NE(sql.count_sql.find("LIKE"), std::string::npos);
                EXPECT_NE(sql.data_sql.find("LIKE"), std::string::npos);
            }
        }

    } // namespace
} // namespace disk::file
