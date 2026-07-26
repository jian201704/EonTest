#pragma once

#include <QSqlDatabase>
#include <QString>

#include "eon/domain/IRecipeRepository.h"

namespace eon::infra {

class SqliteRecipeRepository : public eon::domain::IRecipeRepository {
public:
    explicit SqliteRecipeRepository(const QString& dbPath);
    ~SqliteRecipeRepository() override;

    bool isOpen() const;

    // --- IRecipeRepository ---
    bool saveTemplate(const eon::domain::ParameterTemplate& tmpl, QString& errorMessage) override;
    bool findTemplateById(const QString& id, eon::domain::ParameterTemplate& tmpl,
                          QString& errorMessage) override;
    QList<eon::domain::ParameterTemplate> findTemplatesByWorkflow(const QString& workflowId,
                                                                    QString* errorMessage) override;
    QList<eon::domain::ParameterTemplate> findAllTemplates(QString* errorMessage) override;
    bool deleteTemplate(const QString& id, QString& errorMessage) override;

    bool saveRecipe(const eon::domain::Recipe& recipe, QString& errorMessage) override;
    bool findRecipeById(const QString& id, eon::domain::Recipe& recipe,
                        QString& errorMessage) override;
    QList<eon::domain::Recipe> findRecipesByWorkflow(const QString& workflowId,
                                                       QString* errorMessage) override;
    QList<eon::domain::Recipe> findRecipesByTemplate(const QString& templateId,
                                                       QString* errorMessage) override;
    QList<eon::domain::Recipe> findAllRecipes(QString* errorMessage) override;
    bool deleteRecipe(const QString& id, QString& errorMessage) override;

    bool saveRecipeVersion(const eon::domain::RecipeVersion& version,
                           QString& errorMessage) override;
    QList<eon::domain::RecipeVersion> findVersionsByRecipe(const QString& recipeId,
                                                             QString* errorMessage) override;
    QVariantMap getRecipeSnapshot(const QString& recipeId, const QString& version,
                                   QString& errorMessage) override;

private:
    bool ensureSchema(QString& errorMessage);
    eon::domain::ParameterTemplate rowToTemplate(const QSqlQuery& q) const;
    eon::domain::Recipe rowToRecipe(const QSqlQuery& q) const;

    QString dbPath_;
    QString connName_;
    bool initialized_ = false;
};

} // namespace eon::infra
