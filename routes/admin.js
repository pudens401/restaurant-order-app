const express = require("express");
const router = express.Router();
const bcrypt = require("bcrypt");
const User = require("../models/User");
const MenuItem = require("../models/MenuItem");
const Order = require("../models/Order");

// ---------- Middleware: Require Login ----------
function requireLogin(req, res, next) {
  if (!req.cookies || !req.cookies.adminUser) {
    return res.redirect("/admin/login");
  }
  next();
}

// ---------- AUTH ROUTES ----------

// Show signup page
router.get("/signup", (req, res) => {
  res.render("admin/signup", { error: null });
});

// Handle signup
router.post("/signup", async (req, res) => {
  const { username, password } = req.body;
  const existing = await User.findOne({ username });
  if (existing) {
    return res.render("admin/signup", { error: "User already exists" });
  }
  const hashed = await bcrypt.hash(password, 10);
  await User.create({ username, password: hashed });
  res.redirect("/admin/login");
});

// Show login page
router.get("/login", (req, res) => {
  res.render("admin/login", { error: null });
});

// Handle login
router.post("/login", async (req, res) => {
  const { username, password } = req.body;
  const user = await User.findOne({ username });
  if (!user) {
    return res.render("admin/login", { error: "Invalid credentials" });
  }
  const match = await bcrypt.compare(password, user.password);
  if (!match) {
    return res.render("admin/login", { error: "Invalid credentials" });
  }
  // set cookie
  res.cookie("adminUser", user._id.toString(), { httpOnly: true });
  res.redirect("/admin/dashboard");
});

// Logout
router.get("/logout", (req, res) => {
  res.clearCookie("adminUser");
  res.redirect("/admin/login");
});

// ---------- DASHBOARD ----------
router.get("/dashboard", requireLogin, async (req, res) => {
  const menuItems = await MenuItem.find().limit(6); // preview only
  const menuCount = await MenuItem.countDocuments();
  const orderCount = await Order.countDocuments();
  const pendingOrders = await Order.countDocuments({ status: "pending" });
  
  // Get user info from cookie
  const userId = req.cookies.adminUser;
  const user = await User.findById(userId);

  res.render("admin/dashboard", {
    user: user || { username: 'Admin' },
    menuItems,
    totalItems: menuCount,
    totalOrders: orderCount,
    pendingOrders,
  });
});

// ---------- MENU ROUTES ----------
router.get("/menu", requireLogin, async (req, res) => {
  const menuItems = await MenuItem.find();
  res.render("admin/menuList", { items: menuItems });
});

router.post("/menu/add", requireLogin, async (req, res) => {
  const { name, price, photo, category } = req.body;
  await MenuItem.create({ name, price, photo, category });
  res.redirect("/admin/menu");
});

router.post("/menu/edit/:id", requireLogin, async (req, res) => {
  const { name, price, photo, category } = req.body;
  await MenuItem.findByIdAndUpdate(req.params.id, { name, price, photo, category });
  res.redirect("/admin/menu");
});

router.post("/menu/delete/:id", requireLogin, async (req, res) => {
  await MenuItem.findByIdAndDelete(req.params.id);
  res.redirect("/admin/menu");
});

// ---------- ORDER ROUTES ----------
router.get("/orders", requireLogin, async (req, res) => {
  const orders = await Order.find()
    .populate("items.menuItem") // populate menu item details
    .sort({ createdAt: -1 });

  res.render("admin/orders", { orders });
});

router.post("/orders/update/:id", requireLogin, async (req, res) => {
  const { status } = req.body;
  await Order.findByIdAndUpdate(req.params.id, { status });
  res.redirect("/admin/orders");
});

module.exports = router;
