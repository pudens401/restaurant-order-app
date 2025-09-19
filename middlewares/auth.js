// middlewares/auth.js
function requireAdmin(req, res, next) {
  if (req.session && req.session.user) {
    return next(); // user is logged in
  }
  return res.redirect("/admin/login");
}

module.exports = requireAdmin;
