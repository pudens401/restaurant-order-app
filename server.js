require("dotenv").config();
const express = require("express");
const mongoose = require("mongoose");
const path = require("path");
const bodyParser = require("body-parser");
const cookieParser = require("cookie-parser");

// Import routes
const adminRoutes = require("./routes/admin");
const guestRoutes = require("./routes/guest");
//const orderRoutes = require("./routes/order");
const session = require("cookie-session");

const app = express();

// ---------- Session Setup ----------
app.use(session({
  name: "session",
  keys: [process.env.SESSION_SECRET || "supersecretkey"],
  maxAge: 24 * 60 * 60 * 1000, // 1 day
}));


// ---------- Middleware ----------
app.use(cookieParser());
app.use(bodyParser.urlencoded({ extended: true }));
app.use(bodyParser.json());

// Set EJS as view engine
app.set("view engine", "ejs");
app.set("views", path.join(__dirname, "views"));

// Serve static files (e.g. Tailwind CSS build, images)
app.use(express.static(path.join(__dirname, "public")));

// ---------- Routes ----------
app.use("/admin", adminRoutes);   // Admin dashboard
app.use("/", guestRoutes);        // Guest routes (menu, cart, checkout)
//app.use("/orders", orderRoutes);  // Orders handling

// ---------- MongoDB Connection ----------
const PORT = process.env.PORT || 3000;
const MONGO_URI = process.env.MONGO_URI || "mongodb://127.0.0.1:27017/restaurantDB";

mongoose.connect(MONGO_URI, {
  useNewUrlParser: true,
  useUnifiedTopology: true,
})
.then(() => {
  console.log("✅ Connected to MongoDB");
  app.listen(PORT, () => console.log(`🚀 Server running on http://localhost:${PORT}`));
})
.catch(err => {
  console.error("❌ MongoDB connection error:", err);
});
