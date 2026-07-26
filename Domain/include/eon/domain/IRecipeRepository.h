#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

#include "eon/domain/ParameterTemplate.h"

namespace eon::domain {

// ============================================================================
// IRecipeRepository — 配方 + 参数模板仓储接口
// ============================================================================
class IRecipeRepository {
public:
    virtual ~IRecipeRepository() = default;

    // --- 参数模板 ---
    virtual bool saveTemplate(const ParameterTemplate& tmpl, QString& errorMessage) = 0;
    virtual bool findTemplateById(const QString& id, ParameterTemplate& tmpl,
                                  QString& errorMessage) = 0;
    virtual QList<ParameterTemplate> findTemplatesByWorkflow(const QString& workflowId,
                                                               QString* errorMessage = nullptr) = 0;
    virtual QList<ParameterTemplate> findAllTemplates(QString* errorMessage = nullptr) = 0;
    virtual bool deleteTemplate(const QString& id, QString& errorMessage) = 0;

    // --- 配方 ---
    virtual bool saveRecipe(const Recipe& recipe, QString& errorMessage) = 0;
    virtual bool findRecipeById(const QString& id, Recipe& recipe,
                                QString& errorMessage) = 0;
    virtual QList<Recipe> findRecipesByWorkflow(const QString& workflowId,
                                                  QString* errorMessage = nullptr) = 0;
    virtual QList<Recipe> findRecipesByTemplate(const QString& templateId,
                                                  QString* errorMessage = nullptr) = 0;
    virtual QList<Recipe> findAllRecipes(QString* errorMessage = nullptr) = 0;
    virtual bool deleteRecipe(const QString& id, QString& errorMessage) = 0;

    // --- 配方版本 ---
    virtual bool saveRecipeVersion(const RecipeVersion& version, QString& errorMessage) = 0;
    virtual QList<RecipeVersion> findVersionsByRecipe(const QString& recipeId,
                                                        QString* errorMessage = nullptr) = 0;
    virtual QVariantMap getRecipeSnapshot(const QString& recipeId, const QString& version,
                                           QString& errorMessage) = 0;
};

} // namespace eon::domain
